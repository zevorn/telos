#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/config.h>

struct config_entry {
    const char *key;
    char *value;
    enum telos_config_origin origin;
};

struct telos_config {
    struct config_entry entries[7];
};

static const char *const keys[] = {
    "agent.provider",
    "agent.model",
    "providers.openai.endpoint",
    "providers.openai.secret",
    "providers.openai.state_mode",
    "state.directory",
    "builder.backend",
};

static const char *const defaults[] = {
    "dev.zevorn.openai-responses",
    "configured-model",
    "https://api.openai.com/v1",
    "secret:provider.openai",
    "local",
    "",
    "container",
};

static const char *const environment_names[] = {
    "TELOS_AGENT_PROVIDER",
    "TELOS_AGENT_MODEL",
    "TELOS_OPENAI_ENDPOINT",
    "TELOS_OPENAI_SECRET",
    "TELOS_OPENAI_STATE_MODE",
    "TELOS_STATE_DIRECTORY",
    "TELOS_BUILDER_BACKEND",
};

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
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

static char *trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text)) {
        text += 1;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
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

static struct config_entry *find_entry(
    struct telos_config *config,
    const char *key
)
{
    for (size_t index = 0; index < 7; ++index) {
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
        return strcmp(value, "native") == 0
            || strcmp(value, "container") == 0;
    }
    if (strcmp(key, "providers.openai.state_mode") == 0) {
        return strcmp(value, "local") == 0
            || strcmp(value, "remote") == 0;
    }
    if (strcmp(key, "providers.openai.secret") == 0) {
        return strncmp(value, "secret:", 7) == 0 && value[7] != '\0';
    }
    if (strcmp(key, "state.directory") == 0) {
        return value[0] == '/';
    }
    return true;
}

static bool assign(
    struct telos_config *config,
    const char *key,
    const char *value,
    enum telos_config_origin origin,
    struct telos_error **error
)
{
    struct config_entry *entry = find_entry(config, key);
    char *copy;

    if (entry == NULL || !value_valid(key, value)) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Configuration key or value is invalid"
        );
        return false;
    }
    copy = copy_text(value);
    if (copy == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Configuration value allocation failed"
        );
        return false;
    }
    free(entry->value);
    entry->value = copy;
    entry->origin = origin;
    return true;
}

static bool apply_file(
    struct telos_config *config,
    const char *path,
    enum telos_config_origin origin,
    struct telos_error **error
)
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
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Configuration file could not be opened"
        );
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

            if (
                size < 3
                || clean[size - 1] != ']'
                || size - 2 >= sizeof(section)
            ) {
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
        if (
            snprintf(key, sizeof(key), "%s.%s", section, clean)
            >= (int)sizeof(key)
        ) {
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
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Configuration file is invalid"
        );
    }
    return valid;
}

struct telos_config *telos_config_load(
    const char *home_directory,
    const char *project_directory,
    struct telos_error **error
)
{
    struct telos_config *config;
    char state_directory[4096];
    char user_path[4096];
    char project_path[4096];

    if (error != NULL) {
        *error = NULL;
    }
    if (
        home_directory == NULL
        || home_directory[0] != '/'
        || project_directory == NULL
        || project_directory[0] != '/'
        || snprintf(
            state_directory,
            sizeof(state_directory),
            "%s/.telos",
            home_directory
        ) >= (int)sizeof(state_directory)
        || snprintf(
            user_path,
            sizeof(user_path),
            "%s/.telos/config.toml",
            home_directory
        ) >= (int)sizeof(user_path)
        || snprintf(
            project_path,
            sizeof(project_path),
            "%s/telos.toml",
            project_directory
        ) >= (int)sizeof(project_path)
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Home and project directories must be absolute"
        );
        return NULL;
    }
    config = calloc(1, sizeof(*config));
    if (config == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Configuration allocation failed"
        );
        return NULL;
    }
    for (size_t index = 0; index < 7; ++index) {
        config->entries[index].key = keys[index];
        config->entries[index].value = copy_text(
            index == 5 ? state_directory : defaults[index]
        );
        config->entries[index].origin = TELOS_CONFIG_DEFAULT;
        if (config->entries[index].value == NULL) {
            telos_config_destroy(config);
            set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Configuration default allocation failed"
            );
            return NULL;
        }
    }
    if (
        !apply_file(config, user_path, TELOS_CONFIG_USER, error)
        || !apply_file(config, project_path, TELOS_CONFIG_PROJECT, error)
    ) {
        telos_config_destroy(config);
        return NULL;
    }
    for (size_t index = 0; index < 7; ++index) {
        const char *value = getenv(environment_names[index]);

        if (
            value != NULL
            && !assign(
                config,
                keys[index],
                value,
                TELOS_CONFIG_ENVIRONMENT,
                error
            )
        ) {
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
    for (size_t index = 0; index < 7; ++index) {
        free(config->entries[index].value);
    }
    free(config);
}

bool telos_config_override(
    struct telos_config *config,
    const char *key,
    const char *value,
    struct telos_error **error
)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || key == NULL || value == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Configuration override arguments are invalid"
        );
        return false;
    }
    return assign(
        config,
        key,
        value,
        TELOS_CONFIG_COMMAND_LINE,
        error
    );
}

const char *telos_config_get(
    const struct telos_config *config,
    const char *key
)
{
    struct config_entry *entry;

    if (config == NULL || key == NULL) {
        return NULL;
    }
    entry = find_entry((struct telos_config *)config, key);
    return entry == NULL ? NULL : entry->value;
}

enum telos_config_origin telos_config_get_origin(
    const struct telos_config *config,
    const char *key
)
{
    struct config_entry *entry;

    if (config == NULL || key == NULL) {
        return 0;
    }
    entry = find_entry((struct telos_config *)config, key);
    return entry == NULL ? 0 : entry->origin;
}
