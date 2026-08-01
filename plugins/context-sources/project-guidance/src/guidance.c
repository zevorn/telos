#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/checked_math.h>
#include <telos/plugins/project_guidance.h>

#define TELOS_GUIDANCE_MAX_SIZE (64U * 1024U)

struct text_buffer {
    char *data;
    size_t size;
    size_t capacity;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool buffer_append(struct text_buffer *buffer, const char *text,
                          size_t size)
{
    size_t required;
    size_t capacity;
    char *data;

    if (!telos_size_add(buffer->size, size, &required) ||
        !telos_size_add(required, 1, &required)) {
        return false;
    }
    if (required > buffer->capacity) {
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
    }
    memcpy(buffer->data + buffer->size, text, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return true;
}

static bool buffer_text(struct text_buffer *buffer, const char *text)
{
    return buffer_append(buffer, text, strlen(text));
}

static char *join_path(const char *directory, const char *name)
{
    size_t directory_size = strlen(directory);
    size_t name_size = strlen(name);
    char *path;

    if (directory_size > SIZE_MAX - name_size - 2) {
        return NULL;
    }
    path = malloc(directory_size + name_size + 2);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, directory_size);
    path[directory_size] = '/';
    memcpy(path + directory_size + 1, name, name_size + 1);
    return path;
}

static char *read_optional_guidance(const char *path,
                                    struct telos_error **error)
{
    FILE *file = fopen(path, "rb");
    long length = -1;
    char *content;

    if (file == NULL && errno == ENOENT) {
        content = calloc(1, 1);
        if (content == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Guidance allocation failed");
        }
        return content;
    }
    if (file == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Guidance file could not be opened");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
    }
    if (length < 0 || (size_t)length > TELOS_GUIDANCE_MAX_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error,
                  length >= 0 && (size_t)length > TELOS_GUIDANCE_MAX_SIZE
                      ? TELOS_ERROR_DOMAIN_ARGUMENT
                      : TELOS_ERROR_DOMAIN_IO,
                  length >= 0 && (size_t)length > TELOS_GUIDANCE_MAX_SIZE
                      ? EFBIG
                      : EIO,
                  "Guidance file size is invalid");
        return NULL;
    }
    content = calloc((size_t)length + 1, 1);
    if (content == NULL ||
        fread(content, 1, (size_t)length, file) != (size_t)length) {
        free(content);
        fclose(file);
        set_error(error,
                  content == NULL ? TELOS_ERROR_DOMAIN_MEMORY
                                  : TELOS_ERROR_DOMAIN_IO,
                  content == NULL ? ENOMEM : EIO,
                  "Guidance file could not be read");
        return NULL;
    }
    fclose(file);
    return content;
}

static bool append_guidance_file(struct text_buffer *buffer,
                                 const char *directory,
                                 struct telos_error **error)
{
    char *path = join_path(directory, "AGENTS.md");
    char *content;
    bool result;

    if (path == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Guidance path allocation failed");
        return false;
    }
    content = read_optional_guidance(path, error);
    free(path);
    if (content == NULL) {
        return false;
    }
    if (content[0] == '\0') {
        free(content);
        return true;
    }
    result = (buffer->size == 0 || buffer_text(buffer, "\n\n")) &&
             buffer_text(buffer, content);
    free(content);
    if (!result) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Guidance merge allocation failed");
    }
    return result;
}

bool telos_guidance_discover(const char *telos_home, const char *project_root,
                             const char *current_directory,
                             char **user_guidance, char **project_guidance,
                             struct telos_error **error)
{
    char *real_root = NULL;
    char *real_current = NULL;
    char *user_path = NULL;
    char *user = NULL;
    struct text_buffer project = {0};
    size_t root_size;
    const char *relative;
    char *directory = NULL;

    if (error != NULL) {
        *error = NULL;
    }
    if (telos_home == NULL || project_root == NULL ||
        current_directory == NULL || user_guidance == NULL ||
        project_guidance == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Guidance discovery arguments are invalid");
        return false;
    }
    *user_guidance = NULL;
    *project_guidance = NULL;
    real_root = realpath(project_root, NULL);
    real_current = realpath(current_directory, NULL);
    if (real_root == NULL || real_current == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Guidance directories could not be resolved");
        goto failure;
    }
    root_size = strlen(real_root);
    if (strncmp(real_current, real_root, root_size) != 0 ||
        (real_current[root_size] != '\0' && real_current[root_size] != '/')) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Current directory is outside the project root");
        goto failure;
    }

    user_path = join_path(telos_home, "AGENTS.md");
    if (user_path == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "User guidance path allocation failed");
        goto failure;
    }
    user = read_optional_guidance(user_path, error);
    if (user == NULL || !append_guidance_file(&project, real_root, error)) {
        goto failure;
    }

    relative = real_current + root_size;
    while (*relative == '/') {
        ++relative;
    }
    directory = malloc(strlen(real_current) + 1);
    if (directory == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Project guidance path allocation failed");
        goto failure;
    }
    memcpy(directory, real_root, root_size + 1);
    while (*relative != '\0') {
        const char *separator = strchr(relative, '/');
        size_t component_size = separator == NULL
                                    ? strlen(relative)
                                    : (size_t)(separator - relative);
        size_t directory_size = strlen(directory);

        directory[directory_size] = '/';
        memcpy(directory + directory_size + 1, relative, component_size);
        directory[directory_size + component_size + 1] = '\0';
        if (!append_guidance_file(&project, directory, error)) {
            goto failure;
        }
        relative =
            separator == NULL ? relative + component_size : separator + 1;
    }
    if (project.data == NULL) {
        project.data = calloc(1, 1);
        if (project.data == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Project guidance allocation failed");
            goto failure;
        }
    }

    free(directory);
    free(user_path);
    free(real_current);
    free(real_root);
    *user_guidance = user;
    *project_guidance = project.data;
    return true;

failure:
    free(directory);
    free(project.data);
    free(user);
    free(user_path);
    free(real_current);
    free(real_root);
    return false;
}

void telos_prompt_string_free(char *value) { free(value); }
