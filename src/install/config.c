#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/array.h>
#include <telos/config.h>

#define TELOS_CONFIG_PATH_SIZE 4096U

enum config_key {
    CONFIG_AGENT_PROVIDER,
    CONFIG_AGENT_MODEL,
    CONFIG_AGENT_ENDPOINT,
    CONFIG_AGENT_THINKING,
    CONFIG_AGENT_STATUS,
    CONFIG_STATE_DIRECTORY,
    CONFIG_BUILDER_BACKEND,
    CONFIG_KEY_COUNT,
};

struct config_entry {
    const char *key;
    char *value;
    enum telos_config_origin origin;
};

struct telos_config {
    struct config_entry entries[CONFIG_KEY_COUNT];
    char user_path[TELOS_CONFIG_PATH_SIZE];
};

static const char *const keys[CONFIG_KEY_COUNT] = {
    "agent.provider",
    "agent.model",
    "agent.endpoint",
    "agent.thinking",
    "agent.status",
    "state.directory",
    "builder.backend",
};

static const char *const defaults[CONFIG_KEY_COUNT] = {
    "openai-responses",
    "unconfigured",
    "https://api.openai.com/v1",
    "off",
    "context,thinking",
    "",
    "container",
};

static const char *const environment_names[CONFIG_KEY_COUNT] = {
    "TELOS_AGENT_PROVIDER",
    "TELOS_AGENT_MODEL",
    "TELOS_AGENT_ENDPOINT",
    "TELOS_AGENT_THINKING",
    "TELOS_AGENT_STATUS",
    "TELOS_STATE_DIRECTORY",
    "TELOS_BUILDER_BACKEND",
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static bool is_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\f' || value == '\v';
}

static char *trim(char *text)
{
    char *end;

    while (*text != '\0' && is_space(*text)) {
        text += 1;
    }
    end = text + strlen(text);
    while (end > text && is_space(end[-1])) {
        end -= 1;
    }
    *end = '\0';
    return text;
}

static char *parse_string(char *value)
{
    size_t size;

    value = trim(value);
    size = strlen(value);
    if (size < 2 || value[0] != '"' || value[size - 1] != '"') {
        return NULL;
    }
    value[size - 1] = '\0';
    return copy_text(value + 1);
}

static struct config_entry *find_entry(struct telos_config *config,
                                       const char *key)
{
    for (size_t index = 0; index < TELOS_ARRAY_SIZE(keys); ++index) {
        if (strcmp(config->entries[index].key, key) == 0) {
            return &config->entries[index];
        }
    }
    return NULL;
}

static bool value_valid(const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    if (strcmp(key, "builder.backend") == 0) {
        return strcmp(value, "native") == 0 || strcmp(value, "container") == 0;
    }
    if (strcmp(key, "state.directory") == 0) {
        return value[0] == '/';
    }
    return true;
}

static bool assign(struct telos_config *config, const char *key,
                   const char *value, enum telos_config_origin origin,
                   struct telos_error **error)
{
    struct config_entry *entry = find_entry(config, key);
    char *copy;

    if (entry == NULL || !value_valid(key, value)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Configuration key or value is invalid");
        return false;
    }
    copy = copy_text(value);
    if (copy == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Configuration value allocation failed");
        return false;
    }
    free(entry->value);
    entry->value = copy;
    entry->origin = origin;
    return true;
}

static bool apply_file(struct telos_config *config, const char *path,
                       enum telos_config_origin origin,
                       struct telos_error **error)
{
    FILE *stream = fopen(path, "rb");
    char *line = NULL;
    size_t capacity = 0;
    char section[128] = {0};
    bool valid = true;

    if (stream == NULL && errno == ENOENT) {
        return true;
    }
    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration file could not be opened");
        return false;
    }
    while (getline(&line, &capacity, stream) >= 0) {
        char *clean = trim(line);
        char *separator;
        char key[256];
        char *value;

        if (clean[0] == '\0' || clean[0] == '#') {
            continue;
        }
        if (clean[0] == '[') {
            size_t size = strlen(clean);

            if (size < 3 || clean[size - 1] != ']' ||
                size - 2 >= sizeof(section)) {
                valid = false;
                break;
            }
            memcpy(section, clean + 1, size - 2);
            section[size - 2] = '\0';
            continue;
        }
        separator = strchr(clean, '=');
        if (separator == NULL || section[0] == '\0') {
            valid = false;
            break;
        }
        *separator = '\0';
        clean = trim(clean);
        if (snprintf(key, sizeof(key), "%s.%s", section, clean) >=
            (int)sizeof(key)) {
            valid = false;
            break;
        }
        value = parse_string(separator + 1);
        if (value == NULL) {
            valid = false;
            break;
        }
        valid = assign(config, key, value, origin, error);
        free(value);
        if (!valid) {
            break;
        }
    }
    if (ferror(stream)) {
        valid = false;
    }
    free(line);
    fclose(stream);
    if (!valid && (error == NULL || *error == NULL)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Configuration file is invalid");
    }
    return valid;
}

static bool config_value_writable(const char *value)
{
    for (size_t index = 0; value != NULL && value[index] != '\0'; ++index) {
        unsigned char character = (unsigned char)value[index];

        if (character < 0x20U || character == 0x7fU || character == '"' ||
            character == '\\') {
            return false;
        }
    }
    return value != NULL && value[0] != '\0';
}

static bool write_config_assignment(FILE *stream, const char *key,
                                    const char *value,
                                    struct telos_error **error)
{
    const char *separator = strrchr(key, '.');

    if (separator == NULL || separator[1] == '\0' ||
        fprintf(stream, "%s = \"", separator + 1) < 0 ||
        fputs(value, stream) == EOF || fputs("\"\n", stream) == EOF) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration file could not be written");
        return false;
    }
    return true;
}

static bool ensure_config_directory(const char *path,
                                    struct telos_error **error)
{
    char directory[TELOS_CONFIG_PATH_SIZE];
    char *separator;

    if (path == NULL || strlen(path) >= sizeof(directory)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Configuration path is too long");
        return false;
    }
    memcpy(directory, path, strlen(path) + 1);
    separator = strrchr(directory, '/');
    if (separator == NULL || separator == directory) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Configuration path is invalid");
        return false;
    }
    *separator = '\0';
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration directory could not be created");
        return false;
    }
    return true;
}

static bool persist_user_value(const struct telos_config *config,
                               const char *key, const char *value,
                               struct telos_error **error)
{
    const char *leaf = strrchr(key, '.');
    FILE *input = NULL;
    FILE *output = NULL;
    char temporary_path[TELOS_CONFIG_PATH_SIZE];
    char section[128] = {0};
    char *line = NULL;
    size_t line_capacity = 0;
    bool found = false;
    bool seen_agent = false;
    bool wrote_any = false;
    bool last_was_newline = true;
    int descriptor = -1;
    bool result = false;

    if (!ensure_config_directory(config->user_path, error) ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.tmp.XXXXXX",
                 config->user_path) >= (int)sizeof(temporary_path)) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Configuration temporary path is too long");
        }
        return false;
    }
    input = fopen(config->user_path, "rb");
    if (input == NULL && errno != ENOENT) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration file could not be opened");
        return false;
    }
    descriptor = mkstemp(temporary_path);
    if (descriptor < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration temporary file could not be created");
        if (input != NULL) {
            fclose(input);
        }
        return false;
    }
    if (fchmod(descriptor, 0600) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration temporary file could not be secured");
        close(descriptor);
        unlink(temporary_path);
        if (input != NULL) {
            fclose(input);
        }
        return false;
    }
    output = fdopen(descriptor, "wb");
    if (output == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration temporary file could not be opened");
        close(descriptor);
        unlink(temporary_path);
        if (input != NULL) {
            fclose(input);
        }
        return false;
    }
    descriptor = -1;
    while (input != NULL && getline(&line, &line_capacity, input) >= 0) {
        char *parsed = copy_text(line);
        char *clean;
        size_t line_size = strlen(line);

        if (parsed == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Configuration line allocation failed");
            break;
        }
        clean = trim(parsed);
        if (clean[0] == '[') {
            size_t section_size = strlen(clean);

            if (section_size < 3 || clean[section_size - 1] != ']' ||
                section_size - 2 >= sizeof(section)) {
                free(parsed);
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Configuration file is invalid");
                break;
            }
            if (strcmp(section, "agent") == 0 && !found) {
                if (wrote_any && !last_was_newline && fputc('\n', output) ==
                                      EOF) {
                    free(parsed);
                    set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "Configuration file could not be written");
                    break;
                }
                if (!write_config_assignment(output, key, value, error)) {
                    free(parsed);
                    break;
                }
                found = true;
            }
            memcpy(section, clean + 1, section_size - 2);
            section[section_size - 2] = '\0';
            seen_agent = seen_agent || strcmp(section, "agent") == 0;
        } else if (strcmp(section, "agent") == 0) {
            char *separator = strchr(clean, '=');

            if (separator != NULL) {
                *separator = '\0';
                if (strcmp(trim(clean), leaf + 1) == 0) {
                    if (!write_config_assignment(output, key, value, error)) {
                        free(parsed);
                        break;
                    }
                    found = true;
                    free(parsed);
                    wrote_any = true;
                    last_was_newline = true;
                    continue;
                }
            }
        }
        if (fwrite(line, 1, line_size, output) != line_size) {
            free(parsed);
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Configuration file could not be written");
            break;
        }
        wrote_any = true;
        last_was_newline = line_size > 0 && line[line_size - 1] == '\n';
        free(parsed);
    }
    if (input != NULL && ferror(input) && (error == NULL || *error == NULL)) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration file could not be read");
    }
    if (input != NULL && fclose(input) != 0 && (error == NULL || *error == NULL)) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Configuration file could not be closed");
    }
    free(line);
    if (error != NULL && *error != NULL) {
        fclose(output);
        unlink(temporary_path);
        return false;
    }
    if (seen_agent && !found) {
        if (wrote_any && !last_was_newline && fputc('\n', output) == EOF) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Configuration file could not be written");
        } else {
            write_config_assignment(output, key, value, error);
        }
    } else if (!seen_agent) {
        if (wrote_any && !last_was_newline && fputc('\n', output) == EOF) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Configuration file could not be written");
        } else if (wrote_any && fputc('\n', output) == EOF) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Configuration file could not be written");
        } else if (fputs("[agent]\n", output) == EOF ||
                   !write_config_assignment(output, key, value, error)) {
            if (error == NULL || *error == NULL) {
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "Configuration file could not be written");
            }
        }
    }
    if (error == NULL || *error == NULL) {
        if (fflush(output) != 0 || fclose(output) != 0) {
            output = NULL;
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Configuration file could not be closed");
        } else {
            output = NULL;
            if (rename(temporary_path, config->user_path) != 0) {
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "Configuration file could not be replaced");
            } else {
                result = true;
            }
        }
    }
    if (output != NULL) {
        fclose(output);
    }
    if (!result) {
        unlink(temporary_path);
    }
    return result;
}

struct telos_config *telos_config_load(const char *home_directory,
                                       const char *project_directory,
                                       struct telos_error **error)
{
    struct telos_config *config;
    char state_directory[4096];
    char user_path[4096];
    char project_path[4096];

    if (error != NULL) {
        *error = NULL;
    }
    if (home_directory == NULL || home_directory[0] != '/' ||
        project_directory == NULL || project_directory[0] != '/' ||
        snprintf(state_directory, sizeof(state_directory), "%s/.telos",
                 home_directory) >= (int)sizeof(state_directory) ||
        snprintf(user_path, sizeof(user_path), "%s/.telos/config.toml",
                 home_directory) >= (int)sizeof(user_path) ||
        snprintf(project_path, sizeof(project_path), "%s/telos.toml",
                 project_directory) >= (int)sizeof(project_path)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Home and project directories must be absolute");
        return NULL;
    }
    config = calloc(1, sizeof(*config));
    if (config == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Configuration allocation failed");
        return NULL;
    }
    memcpy(config->user_path, user_path, strlen(user_path) + 1);
    for (size_t index = 0; index < TELOS_ARRAY_SIZE(keys); ++index) {
        config->entries[index].key = keys[index];
        config->entries[index].value =
            copy_text(index == CONFIG_STATE_DIRECTORY ? state_directory
                                                      : defaults[index]);
        config->entries[index].origin = TELOS_CONFIG_DEFAULT;
        if (config->entries[index].value == NULL) {
            telos_config_destroy(config);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Configuration default allocation failed");
            return NULL;
        }
    }
    if (!apply_file(config, user_path, TELOS_CONFIG_USER, error) ||
        !apply_file(config, project_path, TELOS_CONFIG_PROJECT, error)) {
        telos_config_destroy(config);
        return NULL;
    }
    for (size_t index = 0; index < TELOS_ARRAY_SIZE(keys); ++index) {
        const char *value = getenv(environment_names[index]);

        if (value != NULL && !assign(config, keys[index], value,
                                     TELOS_CONFIG_ENVIRONMENT, error)) {
            telos_config_destroy(config);
            return NULL;
        }
    }
    return config;
}

void telos_config_destroy(struct telos_config *config)
{
    if (config == NULL) {
        return;
    }
    for (size_t index = 0; index < TELOS_ARRAY_SIZE(keys); ++index) {
        free(config->entries[index].value);
    }
    free(config);
}

bool telos_config_override(struct telos_config *config, const char *key,
                           const char *value, struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || key == NULL || value == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Configuration override arguments are invalid");
        return false;
    }
    return assign(config, key, value, TELOS_CONFIG_COMMAND_LINE, error);
}

const char *telos_config_get(const struct telos_config *config, const char *key)
{
    struct config_entry *entry;

    if (config == NULL || key == NULL) {
        return NULL;
    }
    entry = find_entry((struct telos_config *)config, key);
    return entry == NULL ? NULL : entry->value;
}

bool telos_config_persist(const struct telos_config *config, const char *key,
                          const char *value, struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || key == NULL || value == NULL ||
        find_entry((struct telos_config *)config, key) == NULL ||
        strncmp(key, "agent.", sizeof("agent.") - 1) != 0 ||
        !value_valid(key, value) || !config_value_writable(value)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Persistent Agent configuration is invalid");
        return false;
    }
    return persist_user_value(config, key, value, error);
}

enum telos_config_origin
telos_config_get_origin(const struct telos_config *config, const char *key)
{
    struct config_entry *entry;

    if (config == NULL || key == NULL) {
        return 0;
    }
    entry = find_entry((struct telos_config *)config, key);
    return entry == NULL ? 0 : entry->origin;
}
