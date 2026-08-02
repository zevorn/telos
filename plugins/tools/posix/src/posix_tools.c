#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <telos/checked_math.h>
#include <telos/plugins/posix_tools.h>
#include <telos/tool.h>

#ifndef PATH_MAX
#define PATH_MAX 4096U
#endif

#define TELOS_POSIX_TOOLS_COUNT 4U
#define TELOS_POSIX_TOOLS_MAX_FILE (1024U * 1024U)
#define TELOS_POSIX_TOOLS_MAX_COMMAND (64U * 1024U)
#define TELOS_POSIX_TOOLS_MAX_OUTPUT (256U * 1024U)

static const char *const read_capabilities[] = {
    "filesystem.read",
};

static const char *const write_capabilities[] = {
    "filesystem.write",
};

static const char *const bash_capabilities[] = {
    "process.spawn",
};

struct text_buffer {
    char *data;
    size_t size;
    size_t capacity;
};

struct telos_posix_tools {
    char working_directory[PATH_MAX];
    char shell[PATH_MAX];
    struct telos_value *schemas[TELOS_POSIX_TOOLS_COUNT];
    struct telos_tool_definition definitions[TELOS_POSIX_TOOLS_COUNT];
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool buffer_reserve(struct text_buffer *buffer,
                           size_t additional,
                           size_t maximum)
{
    size_t required;
    size_t capacity;
    char *data;

    if (!telos_size_add(buffer->size, additional, &required) ||
        !telos_size_add(required, 1, &required) || required > maximum) {
        return false;
    }
    if (required <= buffer->capacity) {
        return true;
    }
    capacity = buffer->capacity == 0 ? 512 : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    data = realloc(buffer->data, capacity);
    if (data == NULL) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append(struct text_buffer *buffer,
                          const char *data,
                          size_t size,
                          size_t maximum)
{
    if (!buffer_reserve(buffer, size, maximum)) {
        return false;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return true;
}

static void buffer_clear(struct text_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

static bool copy_string(char *destination, size_t destination_size,
                        const char *source)
{
    int written;

    written = snprintf(destination, destination_size, "%s", source);
    return written >= 0 && (size_t)written < destination_size;
}

static bool path_within(const char *root, const char *path)
{
    size_t root_size = strlen(root);

    return strcmp(root, "/") == 0 ||
           (strncmp(root, path, root_size) == 0 &&
            (path[root_size] == '\0' || path[root_size] == '/'));
}

static bool make_candidate(const struct telos_posix_tools *tools,
                           const char *path,
                           char candidate[PATH_MAX],
                           struct telos_error **error)
{
    if (path == NULL || path[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool path must not be empty");
        return false;
    }
    if (path[0] == '/') {
        if (!copy_string(candidate, PATH_MAX, path)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Tool path is too long");
            return false;
        }
        return true;
    }
    if (snprintf(candidate, PATH_MAX, "%s/%s", tools->working_directory,
                 path) >= (int)PATH_MAX) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Tool path is too long");
        return false;
    }
    return true;
}

static bool resolve_existing_path(const struct telos_posix_tools *tools,
                                  const char *path,
                                  char resolved[PATH_MAX],
                                  struct telos_error **error)
{
    char candidate[PATH_MAX];

    if (!make_candidate(tools, path, candidate, error)) {
        return false;
    }
    if (realpath(candidate, resolved) == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool path could not be resolved");
        return false;
    }
    if (!path_within(tools->working_directory, resolved)) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Tool path escapes the working directory");
        return false;
    }
    return true;
}

static bool resolve_write_path(const struct telos_posix_tools *tools,
                               const char *path,
                               char resolved[PATH_MAX],
                               struct telos_error **error)
{
    char candidate[PATH_MAX];
    char parent[PATH_MAX];
    char parent_resolved[PATH_MAX];
    char *separator;
    const char *base;

    if (!make_candidate(tools, path, candidate, error)) {
        return false;
    }
    if (realpath(candidate, resolved) != NULL) {
        if (!path_within(tools->working_directory, resolved)) {
            set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                      "Tool path escapes the working directory");
            return false;
        }
        return true;
    }
    if (errno != ENOENT || !copy_string(parent, sizeof(parent), candidate)) {
        set_error(error, TELOS_ERROR_DOMAIN_IO,
                  errno == ENOENT ? ENAMETOOLONG : errno,
                  "Tool path could not be resolved");
        return false;
    }
    separator = strrchr(parent, '/');
    if (separator == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool path has no parent directory");
        return false;
    }
    base = separator + 1;
    if (base[0] == '\0' || strchr(base, '/') != NULL ||
        strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool path filename is invalid");
        return false;
    }
    *separator = '\0';
    if (parent[0] == '\0') {
        parent[0] = '/';
        parent[1] = '\0';
    }
    if (realpath(parent, parent_resolved) == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool parent directory could not be resolved");
        return false;
    }
    if (!path_within(tools->working_directory, parent_resolved) ||
        snprintf(resolved, PATH_MAX, "%s/%s", parent_resolved, base) >=
            (int)PATH_MAX) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Tool path escapes the working directory");
        return false;
    }
    return true;
}

static bool read_file(const char *path,
                      char **content,
                      size_t *content_size,
                      struct telos_error **error)
{
    struct stat status;
    char *buffer;
    size_t size;
    size_t offset = 0;
    int descriptor;

    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool file could not be opened");
        return false;
    }
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || (uintmax_t)status.st_size >
                                  TELOS_POSIX_TOOLS_MAX_FILE) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EFBIG,
                  "Tool file is not a bounded regular file");
        return false;
    }
    size = (size_t)status.st_size;
    buffer = malloc(size + 1);
    if (buffer == NULL) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Tool file buffer allocation failed");
        return false;
    }
    while (offset < size) {
        ssize_t count = read(descriptor, buffer + offset, size - offset);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            free(buffer);
            close(descriptor);
            set_error(error, TELOS_ERROR_DOMAIN_IO, count == 0 ? EIO : errno,
                      "Tool file could not be read");
            return false;
        }
        offset += (size_t)count;
    }
    buffer[size] = '\0';
    close(descriptor);
    *content = buffer;
    *content_size = size;
    return true;
}

static bool write_file(const char *path,
                       const char *content,
                       size_t content_size,
                       struct telos_error **error)
{
    struct stat status;
    size_t offset = 0;
    int descriptor;

    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC |
                                O_NOFOLLOW,
                       0600);
    if (descriptor < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool file could not be opened for writing");
        return false;
    }
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        close(descriptor);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EINVAL,
                  "Tool write target is not a regular file");
        return false;
    }
    while (offset < content_size) {
        ssize_t count = write(descriptor, content + offset,
                              content_size - offset);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            close(descriptor);
            set_error(error, TELOS_ERROR_DOMAIN_IO, count == 0 ? EIO : errno,
                      "Tool file could not be written");
            return false;
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool file could not be closed");
        return false;
    }
    return true;
}

static const char *argument_string(const struct telos_value *arguments,
                                   const char *name)
{
    return telos_value_string(telos_value_get(arguments, name));
}

static bool argument_integer(const struct telos_value *arguments,
                             const char *name,
                             int64_t *value,
                             struct telos_error **error)
{
    const struct telos_value *member = telos_value_get(arguments, name);

    if (member == NULL) {
        return true;
    }
    if (!telos_value_integer(member, value) || *value < 1) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool line range must be a positive integer");
        return false;
    }
    return true;
}

static bool argument_boolean(const struct telos_value *arguments,
                             const char *name,
                             bool *value,
                             struct telos_error **error)
{
    const struct telos_value *member = telos_value_get(arguments, name);

    if (member == NULL) {
        return true;
    }
    if (!telos_value_boolean(member, value)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool boolean argument is invalid");
        return false;
    }
    return true;
}

static bool make_object_result(const char *const *keys,
                               const struct telos_value *const *values,
                               size_t count,
                               struct telos_value **result,
                               struct telos_error **error)
{
    *result = telos_value_new_object(keys, values, count);
    if (*result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Tool result allocation failed");
        return false;
    }
    return true;
}

static bool execute_read(const struct telos_tool_context *context,
                         const struct telos_value *arguments,
                         struct telos_value **result,
                         struct telos_error **error)
{
    const struct telos_posix_tools *tools = context->context;
    const char *path = argument_string(arguments, "path");
    char resolved[PATH_MAX];
    char *content = NULL;
    char *selected = NULL;
    size_t content_size;
    int64_t line_start = 1;
    int64_t line_end = INT64_MAX;
    struct text_buffer buffer = {0};

    if (tools == NULL || path == NULL ||
        !argument_integer(arguments, "line_start", &line_start, error) ||
        !argument_integer(arguments, "line_end", &line_end, error) ||
        line_end < line_start ||
        !resolve_existing_path(tools, path, resolved, error) ||
        !read_file(resolved, &content, &content_size, error)) {
        return false;
    }
    if (telos_value_get(arguments, "line_start") == NULL &&
        telos_value_get(arguments, "line_end") == NULL) {
        selected = content;
        content = NULL;
    } else {
        int64_t line = 1;
        size_t start = 0;

        for (size_t index = 0; index <= content_size; ++index) {
            if (index == content_size || content[index] == '\n') {
                if (line >= line_start && line <= line_end &&
                    !buffer_append(&buffer, content + start,
                                   index - start +
                                       (index < content_size ? 1 : 0),
                                   TELOS_POSIX_TOOLS_MAX_OUTPUT)) {
                    free(content);
                    buffer_clear(&buffer);
                    set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "Tool line selection is too large");
                    return false;
                }
                if (line >= line_end || index == content_size) {
                    break;
                }
                line += 1;
                start = index + 1;
            }
        }
        selected = buffer.data;
        buffer.data = NULL;
        buffer_clear(&buffer);
    }
    {
        struct telos_value *path_value = telos_value_new_string(path);
        struct telos_value *content_value =
            telos_value_new_string(selected == NULL ? "" : selected);
        const char *keys[] = {"path", "content"};
        const struct telos_value *values[] = {path_value, content_value};

        if (path_value == NULL || content_value == NULL ||
            !make_object_result(keys, values, 2, result, error)) {
            telos_value_release(content_value);
            telos_value_release(path_value);
            free(selected);
            free(content);
            return false;
        }
        telos_value_release(content_value);
        telos_value_release(path_value);
    }
    free(selected);
    free(content);
    return true;
}

static bool execute_write(const struct telos_tool_context *context,
                          const struct telos_value *arguments,
                          struct telos_value **result,
                          struct telos_error **error)
{
    const struct telos_posix_tools *tools = context->context;
    const char *path = argument_string(arguments, "path");
    const char *content = argument_string(arguments, "content");
    char resolved[PATH_MAX];
    struct telos_value *path_value;
    struct telos_value *written_value;
    const char *keys[] = {"path", "bytes"};
    const struct telos_value *values[2];

    if (tools == NULL || path == NULL || content == NULL ||
        strlen(content) > TELOS_POSIX_TOOLS_MAX_FILE ||
        !resolve_write_path(tools, path, resolved, error) ||
        !write_file(resolved, content, strlen(content), error)) {
        if (content != NULL && strlen(content) > TELOS_POSIX_TOOLS_MAX_FILE) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                      "Tool write content is too large");
        }
        return false;
    }
    path_value = telos_value_new_string(path);
    written_value = telos_value_new_integer((int64_t)strlen(content));
    values[0] = path_value;
    values[1] = written_value;
    if (path_value == NULL || written_value == NULL ||
        !make_object_result(keys, values, 2, result, error)) {
        telos_value_release(written_value);
        telos_value_release(path_value);
        return false;
    }
    telos_value_release(written_value);
    telos_value_release(path_value);
    return true;
}

static bool execute_edit(const struct telos_tool_context *context,
                         const struct telos_value *arguments,
                         struct telos_value **result,
                         struct telos_error **error)
{
    const struct telos_posix_tools *tools = context->context;
    const char *path = argument_string(arguments, "path");
    const char *old_text = argument_string(arguments, "old_text");
    const char *new_text = argument_string(arguments, "new_text");
    char resolved[PATH_MAX];
    char *content = NULL;
    char *replacement = NULL;
    size_t content_size;
    size_t old_size;
    size_t new_size;
    size_t matches = 0;
    bool replace_all = false;

    if (tools == NULL || path == NULL || old_text == NULL || new_text == NULL ||
        old_text[0] == '\0' || !argument_boolean(arguments, "replace_all",
                                                   &replace_all, error) ||
        !resolve_existing_path(tools, path, resolved, error) ||
        !read_file(resolved, &content, &content_size, error)) {
        return false;
    }
    old_size = strlen(old_text);
    new_size = strlen(new_text);
    for (char *cursor = content;
         (cursor = strstr(cursor, old_text)) != NULL; cursor += old_size) {
        matches += 1;
    }
    if (matches == 0 || (!replace_all && matches != 1)) {
        free(content);
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  matches == 0 ? "Tool edit text was not found"
                               : "Tool edit text is ambiguous");
        return false;
    }
    if (matches > 0 && new_size > old_size &&
        (new_size - old_size) >
            (TELOS_POSIX_TOOLS_MAX_FILE - content_size) / matches) {
        free(content);
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "Tool edit result is too large");
        return false;
    }
    {
        size_t replacement_size = content_size;

        if (new_size >= old_size) {
            replacement_size += matches * (new_size - old_size);
        } else {
            replacement_size -= matches * (old_size - new_size);
        }
        replacement = malloc(replacement_size + 1);
        if (replacement == NULL) {
            free(content);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Tool edit buffer allocation failed");
            return false;
        }
        {
            char *source = content;
            char *destination = replacement;
            size_t remaining = content_size;

            while (remaining > 0) {
                char *match = strstr(source, old_text);
                size_t prefix;

                if (match == NULL ||
                    (!replace_all && destination != replacement)) {
                    memcpy(destination, source, remaining);
                    destination += remaining;
                    break;
                }
                prefix = (size_t)(match - source);
                memcpy(destination, source, prefix);
                destination += prefix;
                memcpy(destination, new_text, new_size);
                destination += new_size;
                source = match + old_size;
                remaining -= prefix + old_size;
            }
            *destination = '\0';
        }
        if (!write_file(resolved, replacement, replacement_size, error)) {
            free(replacement);
            free(content);
            return false;
        }
    }
    {
        struct telos_value *path_value = telos_value_new_string(path);
        struct telos_value *matches_value =
            telos_value_new_integer((int64_t)matches);
        const char *keys[] = {"path", "replacements"};
        const struct telos_value *values[] = {path_value, matches_value};

        if (path_value == NULL || matches_value == NULL ||
            !make_object_result(keys, values, 2, result, error)) {
            telos_value_release(matches_value);
            telos_value_release(path_value);
            free(replacement);
            free(content);
            return false;
        }
        telos_value_release(matches_value);
        telos_value_release(path_value);
    }
    free(replacement);
    free(content);
    return true;
}

static bool execute_bash(const struct telos_tool_context *context,
                         const struct telos_value *arguments,
                         struct telos_value **result,
                         struct telos_error **error)
{
    const struct telos_posix_tools *tools = context->context;
    const char *command = argument_string(arguments, "command");
    int descriptors[2] = {-1, -1};
    pid_t child;
    struct text_buffer output = {0};
    int status = 0;
    bool child_done = false;
    bool stream_done = false;

    if (tools == NULL || command == NULL || command[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool shell command must not be empty");
        return false;
    }
    if (strlen(command) > TELOS_POSIX_TOOLS_MAX_COMMAND) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Tool shell command is too long");
        return false;
    }
    if (pipe(descriptors) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool shell pipe could not be created");
        return false;
    }
    child = fork();
    if (child < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Tool shell process could not be created");
        return false;
    }
    if (child == 0) {
        int flags;

        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0 ||
            dup2(descriptors[1], STDERR_FILENO) < 0 ||
            chdir(tools->working_directory) != 0) {
            _exit(126);
        }
        flags = fcntl(descriptors[1], F_GETFD);
        if (flags >= 0) {
            fcntl(descriptors[1], F_SETFD, flags & ~FD_CLOEXEC);
        }
        close(descriptors[1]);
        execl(tools->shell, tools->shell, "-c", command, (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    {
        int flags = fcntl(descriptors[0], F_GETFL);

        if (flags >= 0) {
            fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK);
        }
    }
    while (!stream_done) {
        struct pollfd descriptor = {
            .fd = descriptors[0],
            .events = POLLIN,
        };
        int poll_result;

        if (context->cancel != NULL &&
            telos_cancel_requested(context->cancel)) {
            if (!child_done) {
                kill(child, SIGTERM);
            }
            close(descriptors[0]);
            if (!child_done) {
                waitpid(child, &status, 0);
            }
            buffer_clear(&output);
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "Tool shell command was cancelled");
            return false;
        }
        poll_result = poll(&descriptor, 1, 100);
        if (poll_result < 0 && errno != EINTR) {
            break;
        }
        if (poll_result > 0 || child_done) {
            char chunk[4096];
            ssize_t count;

            do {
                count = read(descriptors[0], chunk, sizeof(chunk));
                if (count > 0 && !buffer_append(&output, chunk,
                                                (size_t)count,
                                                TELOS_POSIX_TOOLS_MAX_OUTPUT)) {
                    kill(child, SIGTERM);
                    waitpid(child, &status, 0);
                    close(descriptors[0]);
                    buffer_clear(&output);
                    set_error(error, TELOS_ERROR_DOMAIN_IO, EFBIG,
                              "Tool shell output is too large");
                    return false;
                }
            } while (count > 0);
            if (count == 0) {
                stream_done = true;
            }
        }
        if (!child_done) {
            pid_t waited = waitpid(child, &status, WNOHANG);

            if (waited == child) {
                child_done = true;
            } else if (waited < 0 && errno != EINTR) {
                break;
            }
        }
    }
    close(descriptors[0]);
    if (!child_done) {
        waitpid(child, &status, 0);
    }
    {
        int64_t exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                                              : -WTERMSIG(status);
        struct telos_value *exit_value = telos_value_new_integer(exit_code);
        struct telos_value *output_value =
            telos_value_new_string(output.data == NULL ? "" : output.data);
        const char *keys[] = {"exit_code", "output"};
        const struct telos_value *values[] = {exit_value, output_value};

        if (exit_value == NULL || output_value == NULL ||
            !make_object_result(keys, values, 2, result, error)) {
            telos_value_release(output_value);
            telos_value_release(exit_value);
            buffer_clear(&output);
            return false;
        }
        telos_value_release(output_value);
        telos_value_release(exit_value);
    }
    buffer_clear(&output);
    return true;
}

static const char read_schema[] =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"line_start\":{\"type\":\"integer\"},\"line_end\":{\"type\":\"integer\"}},"
    "\"required\":[\"path\"]}";
static const char write_schema[] =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}";
static const char edit_schema[] =
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"old_text\":{\"type\":\"string\"},\"new_text\":{\"type\":\"string\"},"
    "\"replace_all\":{\"type\":\"boolean\"}},"
    "\"required\":[\"path\",\"old_text\",\"new_text\"]}";
static const char bash_schema[] =
    "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"}},"
    "\"required\":[\"command\"]}";

static const char descriptions[] =
    "[{\"name\":\"read\",\"description\":\"Read a bounded text file "
    "from the project.\",\"parameters\":"
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"line_start\":{\"type\":\"integer\"},\"line_end\":{\"type\":\"integer\"}},"
    "\"required\":[\"path\"]}},{\"name\":\"write\",\"description\":"
    "\"Write a bounded text file in the project.\",\"parameters\":"
    "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},"
    "\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}},"
    "{\"name\":\"edit\",\"description\":\"Replace exact text in a project "
    "file.\",\"parameters\":{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},\"old_text\":{\"type\":\"string\"},"
    "\"new_text\":{\"type\":\"string\"},\"replace_all\":{\"type\":\"boolean\"}},"
    "\"required\":[\"path\",\"old_text\",\"new_text\"]}},{\"name\":\"bash\","
    "\"description\":\"Run a bounded shell command in the project.\","
    "\"parameters\":{\"type\":\"object\",\"properties\":{\"command\":{"
    "\"type\":\"string\"}},\"required\":[\"command\"]}}]";

struct telos_posix_tools *
telos_posix_tools_create(const struct telos_posix_tools_config *config,
                         struct telos_error **error)
{
    static const char *const schemas[] = {
        read_schema,
        write_schema,
        edit_schema,
        bash_schema,
    };
    static const struct telos_tool_definition definitions[] = {
        {
            .id = "read",
            .required_capabilities = read_capabilities,
            .required_capability_count = 1,
            .execute = execute_read,
        },
        {
            .id = "write",
            .required_capabilities = write_capabilities,
            .required_capability_count = 1,
            .execute = execute_write,
        },
        {
            .id = "edit",
            .required_capabilities = write_capabilities,
            .required_capability_count = 1,
            .execute = execute_edit,
        },
        {
            .id = "bash",
            .required_capabilities = bash_capabilities,
            .required_capability_count = 1,
            .execute = execute_bash,
        },
    };
    struct telos_posix_tools *tools;
    char working_directory[PATH_MAX];

    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || config->working_directory == NULL ||
        realpath(config->working_directory, working_directory) == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "POSIX Tools working directory is invalid");
        return NULL;
    }
    tools = calloc(1, sizeof(*tools));
    if (tools == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "POSIX Tools allocation failed");
        return NULL;
    }
    if (!copy_string(tools->working_directory, sizeof(tools->working_directory),
                     working_directory) ||
        !copy_string(tools->shell, sizeof(tools->shell),
                     config->shell == NULL ? "/bin/sh" : config->shell)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "POSIX Tools path is too long");
        telos_posix_tools_destroy(tools);
        return NULL;
    }
    for (size_t index = 0; index < TELOS_POSIX_TOOLS_COUNT; ++index) {
        tools->schemas[index] = telos_value_parse_json(
            schemas[index], strlen(schemas[index]), error);
        if (tools->schemas[index] == NULL) {
            telos_posix_tools_destroy(tools);
            return NULL;
        }
        tools->definitions[index] = definitions[index];
        tools->definitions[index].input_schema = tools->schemas[index];
        tools->definitions[index].context = tools;
    }
    return tools;
}

void telos_posix_tools_destroy(struct telos_posix_tools *tools)
{
    if (tools == NULL) {
        return;
    }
    for (size_t index = 0; index < TELOS_POSIX_TOOLS_COUNT; ++index) {
        telos_value_release(tools->schemas[index]);
    }
    free(tools);
}

bool telos_posix_tools_register(struct telos_posix_tools *tools,
                                struct telos_registry *registry,
                                struct telos_error **error)
{
    struct telos_registry_transaction *transaction;

    if (error != NULL) {
        *error = NULL;
    }
    if (tools == NULL || registry == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "POSIX Tools registration arguments are invalid");
        return false;
    }
    transaction = telos_registry_transaction_begin(
        registry, "dev.zevorn.posix-tools", error);
    if (transaction == NULL) {
        return false;
    }
    for (size_t index = 0; index < TELOS_POSIX_TOOLS_COUNT; ++index) {
        const struct telos_extension_descriptor descriptor = {
            .id = tools->definitions[index].id,
            .kind = TELOS_EXTENSION_TOOL,
            .required_capabilities =
                tools->definitions[index].required_capabilities,
            .required_capability_count =
                tools->definitions[index].required_capability_count,
            .implementation = &tools->definitions[index],
        };

        if (!telos_registry_transaction_add(transaction, &descriptor, error)) {
            telos_registry_transaction_abort(transaction);
            return false;
        }
    }
    if (!telos_registry_transaction_commit(transaction, error)) {
        telos_registry_transaction_abort(transaction);
        return false;
    }
    return true;
}

struct telos_value *telos_posix_tools_describe(struct telos_error **error)
{
    return telos_value_parse_json(descriptions, strlen(descriptions), error);
}
