#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/array.h>
#include <telos/manifest.h>
#include <telos/plugin.h>

#include "sha256.h"

#define MANIFEST_MAX_SIZE (1024U * 1024U)

struct telos_plugin_manifest {
    char *id;
    char *name;
    char *version;
    char *entry;
    uint32_t abi;
    unsigned int runtime_modes;
    enum telos_plugin_runtime_mode default_runtime;
    char **targets;
    size_t target_count;
    char **permissions;
    size_t permission_count;
};

struct telos_plugin_lock {
    char *source_hash;
    struct telos_lock_dependency *dependencies;
    size_t dependency_count;
};

enum manifest_section {
    SECTION_NONE = 0,
    SECTION_PLUGIN,
    SECTION_RUNTIME,
    SECTION_PLATFORM,
    SECTION_BUILD,
    SECTION_PERMISSIONS,
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *read_text_file(const char *path, struct telos_error **error)
{
    FILE *stream;
    long length = -1;
    char *content;
    size_t received;
    int close_result;

    if (path == NULL || path[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Manifest path is required");
        return NULL;
    }
    stream = fopen(path, "rb");
    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Manifest file could not be opened");
        return NULL;
    }
    if (fseek(stream, 0, SEEK_END) == 0) {
        length = ftell(stream);
    }
    if (length < 0 || length > (long)MANIFEST_MAX_SIZE ||
        fseek(stream, 0, SEEK_SET) != 0) {
        bool too_large = length >= 0 && length > (long)MANIFEST_MAX_SIZE;

        fclose(stream);
        set_error(error, TELOS_ERROR_DOMAIN_IO, too_large ? EFBIG : EIO,
                  "Manifest file size is invalid");
        return NULL;
    }
    content = calloc((size_t)length + 1, 1);
    if (content == NULL) {
        fclose(stream);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Manifest file allocation failed");
        return NULL;
    }
    errno = 0;
    received = fread(content, 1, (size_t)length, stream);
    close_result = fclose(stream);
    if (received != (size_t)length || close_result != 0) {
        int saved_errno = errno;

        free(content);
        set_error(error, TELOS_ERROR_DOMAIN_IO,
                  saved_errno == 0 ? EIO : saved_errno,
                  "Manifest file could not be read");
        return NULL;
    }
    return content;
}

static char *trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text += 1;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end -= 1;
    }
    *end = '\0';
    return text;
}

static char *strip_comment(char *line)
{
    bool in_string = false;

    for (char *cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' && (cursor == line || cursor[-1] != '\\')) {
            in_string = !in_string;
        } else if (*cursor == '#' && !in_string) {
            *cursor = '\0';
            break;
        }
    }
    return trim(line);
}

static bool split_assignment(char *line, char **key, char **value)
{
    bool in_string = false;

    line = strip_comment(line);
    if (line[0] == '\0') {
        return false;
    }
    for (char *cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '=' && !in_string) {
            *cursor = '\0';
            *key = trim(line);
            *value = trim(cursor + 1);
            return (*key)[0] != '\0' && (*value)[0] != '\0';
        }
        if (*cursor == '"' && (cursor == line || cursor[-1] != '\\')) {
            in_string = !in_string;
        }
    }
    return false;
}

static bool array_complete(const char *value)
{
    bool in_string = false;
    bool escaped = false;

    if (value == NULL || value[0] != '[') {
        return true;
    }
    for (const char *cursor = value + 1; *cursor != '\0'; ++cursor) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (*cursor == '\\' && in_string) {
            escaped = true;
        } else if (*cursor == '"') {
            in_string = !in_string;
        } else if (*cursor == ']' && !in_string) {
            return true;
        }
    }
    return false;
}

static bool append_text(char **text, size_t *size, const char *addition)
{
    size_t addition_size = strlen(addition);
    char *next;

    if (*size > MANIFEST_MAX_SIZE - 2 ||
        addition_size > MANIFEST_MAX_SIZE - *size - 2) {
        return false;
    }
    next = realloc(*text, *size + addition_size + 2);
    if (next == NULL) {
        return false;
    }
    next[*size] = ' ';
    memcpy(next + *size + 1, addition, addition_size + 1);
    *text = next;
    *size += addition_size + 1;
    return true;
}

static char *parse_string(const char *value)
{
    size_t size;
    char *result;
    size_t output = 0;

    if (value == NULL || value[0] != '"') {
        return NULL;
    }
    size = strlen(value);
    if (size < 2 || value[size - 1] != '"') {
        return NULL;
    }
    result = malloc(size);
    if (result == NULL) {
        return NULL;
    }
    for (size_t index = 1; index + 1 < size; ++index) {
        if (value[index] == '\\') {
            index += 1;
            if (index + 1 >= size ||
                (value[index] != '\\' && value[index] != '"')) {
                free(result);
                return NULL;
            }
        }
        result[output++] = value[index];
    }
    result[output] = '\0';
    return result;
}

static void string_array_clear(char **values, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        free(values[index]);
    }
    free(values);
}

static bool parse_string_array(const char *value, char ***items, size_t *count)
{
    const char *cursor = value;
    char **result = NULL;
    size_t result_count = 0;

    while (isspace((unsigned char)*cursor)) {
        cursor += 1;
    }
    if (*cursor++ != '[') {
        return false;
    }
    for (;;) {
        const char *start;
        const char *end;
        char *encoded;
        char *item;
        char **next;

        while (isspace((unsigned char)*cursor)) {
            cursor += 1;
        }
        if (*cursor == ']') {
            cursor += 1;
            break;
        }
        if (*cursor != '"') {
            goto failure;
        }
        start = cursor;
        cursor += 1;
        while (*cursor != '\0') {
            if (*cursor == '"' && cursor[-1] != '\\') {
                break;
            }
            cursor += 1;
        }
        if (*cursor != '"') {
            goto failure;
        }
        end = cursor;
        encoded = malloc((size_t)(end - start) + 2);
        if (encoded == NULL) {
            goto failure;
        }
        memcpy(encoded, start, (size_t)(end - start) + 1);
        encoded[(size_t)(end - start) + 1] = '\0';
        item = parse_string(encoded);
        free(encoded);
        if (item == NULL) {
            goto failure;
        }
        if (result_count == SIZE_MAX / sizeof(*next)) {
            free(item);
            goto failure;
        }
        next = realloc(result, (result_count + 1) * sizeof(*next));
        if (next == NULL) {
            free(item);
            goto failure;
        }
        result = next;
        result[result_count++] = item;
        cursor += 1;
        while (isspace((unsigned char)*cursor)) {
            cursor += 1;
        }
        if (*cursor == ',') {
            cursor += 1;
            continue;
        }
        if (*cursor == ']') {
            cursor += 1;
            break;
        }
        goto failure;
    }
    while (isspace((unsigned char)*cursor)) {
        cursor += 1;
    }
    if (*cursor != '\0') {
        goto failure;
    }
    if (result == NULL) {
        result = calloc(1, sizeof(*result));
        if (result == NULL) {
            goto failure;
        }
    }
    *items = result;
    *count = result_count;
    return true;

failure:
    string_array_clear(result, result_count);
    return false;
}

static bool id_valid(const char *id)
{
    bool saw_separator = false;
    bool need_character = true;

    if (id == NULL || id[0] == '\0') {
        return false;
    }
    for (const char *cursor = id; *cursor != '\0'; ++cursor) {
        if (*cursor == '.' || *cursor == '-') {
            if (need_character) {
                return false;
            }
            saw_separator = true;
            need_character = true;
        } else if ((*cursor >= 'a' && *cursor <= 'z') ||
                   (*cursor >= '0' && *cursor <= '9')) {
            need_character = false;
        } else {
            return false;
        }
    }
    return saw_separator && !need_character;
}

static bool permission_valid(const char *permission)
{
    bool after_colon = false;

    if (permission == NULL || permission[0] < 'a' || permission[0] > 'z') {
        return false;
    }
    for (const char *cursor = permission + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == ':' && !after_colon && cursor[1] != '\0') {
            after_colon = true;
        } else if ((*cursor >= 'a' && *cursor <= 'z') ||
                   (*cursor >= 'A' && *cursor <= 'Z' && after_colon) ||
                   (*cursor >= '0' && *cursor <= '9') || *cursor == '.' ||
                   *cursor == '-' || (*cursor == '_' && after_colon)) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static enum telos_plugin_runtime_mode runtime_mode(const char *name)
{
    if (strcmp(name, "builtin") == 0) {
        return TELOS_PLUGIN_RUNTIME_BUILTIN;
    }
    if (strcmp(name, "inprocess") == 0) {
        return TELOS_PLUGIN_RUNTIME_INPROCESS;
    }
    if (strcmp(name, "process") == 0) {
        return TELOS_PLUGIN_RUNTIME_PROCESS;
    }
    if (strcmp(name, "static") == 0) {
        return TELOS_PLUGIN_RUNTIME_STATIC;
    }
    return 0;
}

static bool target_valid(const char *target)
{
    static const char *targets[] = {
        "linux-x86_64", "linux-aarch64", "linux-riscv64",
        "darwin-x86_64", "darwin-aarch64",
        "zephyr-arm64", "zephyr-native",
    };

    for (size_t index = 0; index < TELOS_ARRAY_SIZE(targets); ++index) {
        if (strcmp(target, targets[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool assign_unique_string(char **field, const char *value)
{
    if (*field != NULL) {
        return false;
    }
    *field = parse_string(value);
    return *field != NULL;
}

static bool parse_manifest_field(struct telos_plugin_manifest *manifest,
                                 enum manifest_section section,
                                 const char *key,
                                 const char *value,
                                 bool *build_seen)
{
    if (section == SECTION_PLUGIN) {
        if (strcmp(key, "id") == 0) {
            return assign_unique_string(&manifest->id, value);
        }
        if (strcmp(key, "name") == 0) {
            return assign_unique_string(&manifest->name, value);
        }
        if (strcmp(key, "version") == 0) {
            return assign_unique_string(&manifest->version, value);
        }
        if (strcmp(key, "entry") == 0) {
            return assign_unique_string(&manifest->entry, value);
        }
        if (strcmp(key, "abi") == 0 && manifest->abi == 0) {
            char *end = NULL;
            unsigned long parsed = strtoul(value, &end, 10);

            if (end == value || *trim(end) != '\0' || parsed > UINT32_MAX) {
                return false;
            }
            manifest->abi = (uint32_t)parsed;
            return true;
        }
        return false;
    }
    if (section == SECTION_RUNTIME && strcmp(key, "modes") == 0) {
        char **modes = NULL;
        size_t count = 0;

        if (manifest->runtime_modes != 0 ||
            !parse_string_array(value, &modes, &count) || count == 0) {
            string_array_clear(modes, count);
            return false;
        }
        for (size_t index = 0; index < count; ++index) {
            enum telos_plugin_runtime_mode mode = runtime_mode(modes[index]);

            if (mode == 0 || (manifest->runtime_modes & mode) != 0) {
                string_array_clear(modes, count);
                return false;
            }
            manifest->runtime_modes |= mode;
        }
        string_array_clear(modes, count);
        return true;
    }
    if (section == SECTION_RUNTIME && strcmp(key, "default") == 0) {
        char *name;

        if (manifest->default_runtime != 0) {
            return false;
        }
        name = parse_string(value);
        if (name == NULL) {
            return false;
        }
        manifest->default_runtime = runtime_mode(name);
        free(name);
        return manifest->default_runtime != 0;
    }
    if (section == SECTION_PLATFORM && strcmp(key, "targets") == 0) {
        if (manifest->targets != NULL ||
            !parse_string_array(value, &manifest->targets,
                                &manifest->target_count) ||
            manifest->target_count == 0) {
            return false;
        }
        for (size_t index = 0; index < manifest->target_count; ++index) {
            if (!target_valid(manifest->targets[index])) {
                return false;
            }
        }
        return true;
    }
    if (section == SECTION_BUILD && strcmp(key, "system") == 0) {
        char *system;
        bool valid;

        if (*build_seen) {
            return false;
        }
        system = parse_string(value);
        valid = system != NULL && strcmp(system, "meson") == 0;
        free(system);
        *build_seen = valid;
        return valid;
    }
    if (section == SECTION_PERMISSIONS && strcmp(key, "required") == 0) {
        if (manifest->permissions != NULL ||
            !parse_string_array(value, &manifest->permissions,
                                &manifest->permission_count)) {
            return false;
        }
        for (size_t index = 0; index < manifest->permission_count; ++index) {
            if (!permission_valid(manifest->permissions[index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static enum manifest_section parse_section(const char *line)
{
    if (strcmp(line, "[plugin]") == 0) {
        return SECTION_PLUGIN;
    }
    if (strcmp(line, "[runtime]") == 0) {
        return SECTION_RUNTIME;
    }
    if (strcmp(line, "[platform]") == 0) {
        return SECTION_PLATFORM;
    }
    if (strcmp(line, "[build]") == 0) {
        return SECTION_BUILD;
    }
    if (strcmp(line, "[permissions]") == 0) {
        return SECTION_PERMISSIONS;
    }
    return SECTION_NONE;
}

struct telos_plugin_manifest *
telos_plugin_manifest_load(const char *path, struct telos_error **error)
{
    struct telos_plugin_manifest *manifest;
    char *content;
    char *save = NULL;
    char *line;
    enum manifest_section section = SECTION_NONE;
    unsigned int sections = 0;
    bool build_seen = false;
    bool valid = true;

    if (error != NULL) {
        *error = NULL;
    }
    content = read_text_file(path, error);
    if (content == NULL) {
        return NULL;
    }
    manifest = calloc(1, sizeof(*manifest));
    if (manifest == NULL) {
        free(content);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Plugin manifest allocation failed");
        return NULL;
    }

    for (line = strtok_r(content, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        char *key;
        char *value;
        char *clean = trim(line);
        char *joined = NULL;

        if (clean[0] == '\0' || clean[0] == '#') {
            continue;
        }
        if (clean[0] == '[') {
            section = parse_section(clean);
            if (section == SECTION_NONE || (sections & (1U << section)) != 0) {
                valid = false;
                break;
            }
            sections |= 1U << section;
            continue;
        }
        if (section == SECTION_NONE || !split_assignment(clean, &key, &value)) {
            valid = false;
            break;
        }
        if (value[0] == '[' && !array_complete(value)) {
            size_t joined_size = strlen(value);

            joined = malloc(joined_size + 1);
            if (joined != NULL) {
                memcpy(joined, value, joined_size + 1);
            }
            while (joined != NULL && !array_complete(joined)) {
                line = strtok_r(NULL, "\n", &save);
                if (line == NULL) {
                    break;
                }
                clean = strip_comment(line);
                if (clean[0] != '\0' &&
                    !append_text(&joined, &joined_size, clean)) {
                    free(joined);
                    joined = NULL;
                    break;
                }
            }
            if (joined == NULL || !array_complete(joined)) {
                free(joined);
                valid = false;
                break;
            }
            value = joined;
        }
        valid =
            parse_manifest_field(manifest, section, key, value, &build_seen);
        free(joined);
        if (!valid) {
            break;
        }
    }
    free(content);

    valid = valid &&
            sections == ((1U << SECTION_PLUGIN) | (1U << SECTION_RUNTIME) |
                         (1U << SECTION_PLATFORM) | (1U << SECTION_BUILD) |
                         (1U << SECTION_PERMISSIONS)) &&
            id_valid(manifest->id) && manifest->name != NULL &&
            manifest->name[0] != '\0' && manifest->version != NULL &&
            manifest->version[0] != '\0' &&
            manifest->abi == TELOS_PLUGIN_ABI_VERSION &&
            manifest->entry != NULL &&
            strcmp(manifest->entry, "telos_plugin_init_v1") == 0 &&
            manifest->runtime_modes != 0 && manifest->default_runtime != 0 &&
            (manifest->runtime_modes & manifest->default_runtime) != 0 &&
            manifest->targets != NULL && build_seen &&
            manifest->permissions != NULL;
    if (!valid) {
        telos_plugin_manifest_destroy(manifest);
        set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EINVAL,
                  "Plugin manifest does not conform to schema v1");
        return NULL;
    }
    return manifest;
}

void telos_plugin_manifest_destroy(struct telos_plugin_manifest *manifest)
{
    if (manifest == NULL) {
        return;
    }
    string_array_clear(manifest->permissions, manifest->permission_count);
    string_array_clear(manifest->targets, manifest->target_count);
    free(manifest->entry);
    free(manifest->version);
    free(manifest->name);
    free(manifest->id);
    free(manifest);
}

const char *
telos_plugin_manifest_id(const struct telos_plugin_manifest *manifest)
{
    return manifest == NULL ? NULL : manifest->id;
}

const char *
telos_plugin_manifest_name(const struct telos_plugin_manifest *manifest)
{
    return manifest == NULL ? NULL : manifest->name;
}

const char *
telos_plugin_manifest_version(const struct telos_plugin_manifest *manifest)
{
    return manifest == NULL ? NULL : manifest->version;
}

uint32_t telos_plugin_manifest_abi(const struct telos_plugin_manifest *manifest)
{
    return manifest == NULL ? 0 : manifest->abi;
}

unsigned int
telos_plugin_manifest_runtime_modes(const telos_plugin_manifest *manifest)
{
    return manifest == NULL ? 0 : manifest->runtime_modes;
}

enum telos_plugin_runtime_mode
telos_plugin_manifest_default_runtime(const telos_plugin_manifest *manifest)
{
    return manifest == NULL ? 0 : manifest->default_runtime;
}

size_t
telos_plugin_manifest_permission_count(const telos_plugin_manifest *manifest)
{
    return manifest == NULL ? 0 : manifest->permission_count;
}

const char *
telos_plugin_manifest_permission_at(const telos_plugin_manifest *manifest,
                                    size_t index)
{
    if (manifest == NULL || index >= manifest->permission_count) {
        return NULL;
    }
    return manifest->permissions[index];
}

static bool hash_valid(const char *hash)
{
    if (hash == NULL || strlen(hash) != 64) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        if (!((hash[index] >= '0' && hash[index] <= '9') ||
              (hash[index] >= 'a' && hash[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool lock_add_dependency(struct telos_plugin_lock *lock,
                                struct telos_lock_dependency **dependency)
{
    struct telos_lock_dependency *next;

    if (lock->dependency_count == SIZE_MAX / sizeof(*next)) {
        return false;
    }
    next = realloc(lock->dependencies,
                   (lock->dependency_count + 1) * sizeof(*next));
    if (next == NULL) {
        return false;
    }
    lock->dependencies = next;
    *dependency = &lock->dependencies[lock->dependency_count++];
    **dependency = (struct telos_lock_dependency){0};
    return true;
}

static void lock_dependency_clear(struct telos_lock_dependency *dependency)
{
    free((char *)dependency->sha256);
    free((char *)dependency->source);
    free((char *)dependency->version);
    free((char *)dependency->name);
}

struct telos_plugin_lock *telos_plugin_lock_load(const char *path,
                                                 struct telos_error **error)
{
    struct telos_plugin_lock *lock;
    struct telos_lock_dependency *dependency = NULL;
    char *content;
    char *save = NULL;
    char *line;
    bool format_seen = false;
    bool dependencies_seen = false;
    bool valid = true;

    if (error != NULL) {
        *error = NULL;
    }
    content = read_text_file(path, error);
    if (content == NULL) {
        return NULL;
    }
    lock = calloc(1, sizeof(*lock));
    if (lock == NULL) {
        free(content);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Plugin lock allocation failed");
        return NULL;
    }
    for (line = strtok_r(content, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        char *key;
        char *value;
        char *clean = trim(line);
        char *parsed;

        if (clean[0] == '\0' || clean[0] == '#') {
            continue;
        }
        if (strcmp(clean, "[[dependency]]") == 0) {
            if (dependency != NULL &&
                (dependency->name == NULL || dependency->version == NULL ||
                 dependency->source == NULL ||
                 !hash_valid(dependency->sha256))) {
                valid = false;
                break;
            }
            if (!lock_add_dependency(lock, &dependency)) {
                valid = false;
                break;
            }
            continue;
        }
        if (!split_assignment(clean, &key, &value)) {
            valid = false;
            break;
        }
        if (dependency == NULL) {
            if (strcmp(key, "format") == 0 && !format_seen) {
                valid = strcmp(value, "1") == 0;
                format_seen = valid;
            } else if (strcmp(key, "source_hash") == 0 &&
                       lock->source_hash == NULL) {
                lock->source_hash = parse_string(value);
                valid = hash_valid(lock->source_hash);
            } else if (strcmp(key, "dependencies") == 0 && !dependencies_seen) {
                valid = strcmp(value, "[]") == 0;
                dependencies_seen = valid;
            } else {
                valid = false;
            }
            if (!valid) {
                break;
            }
            continue;
        }
        parsed = parse_string(value);
        if (parsed == NULL) {
            valid = false;
            break;
        }
        if (strcmp(key, "name") == 0 && dependency->name == NULL) {
            dependency->name = parsed;
        } else if (strcmp(key, "version") == 0 && dependency->version == NULL) {
            dependency->version = parsed;
        } else if (strcmp(key, "source") == 0 && dependency->source == NULL) {
            dependency->source = parsed;
        } else if (strcmp(key, "sha256") == 0 && dependency->sha256 == NULL) {
            dependency->sha256 = parsed;
        } else {
            free(parsed);
            valid = false;
            break;
        }
    }
    free(content);
    valid = valid && format_seen && lock->source_hash != NULL &&
            (dependencies_seen || lock->dependency_count > 0) &&
            (dependency == NULL ||
             (dependency->name != NULL && dependency->version != NULL &&
              dependency->source != NULL && hash_valid(dependency->sha256)));
    if (!valid) {
        telos_plugin_lock_destroy(lock);
        set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EINVAL,
                  "Plugin lock does not conform to schema v1");
        return NULL;
    }
    return lock;
}

void telos_plugin_lock_destroy(struct telos_plugin_lock *lock)
{
    if (lock == NULL) {
        return;
    }
    for (size_t index = 0; index < lock->dependency_count; ++index) {
        lock_dependency_clear(&lock->dependencies[index]);
    }
    free(lock->dependencies);
    free(lock->source_hash);
    free(lock);
}

const char *telos_plugin_lock_source_hash(const struct telos_plugin_lock *lock)
{
    return lock == NULL ? NULL : lock->source_hash;
}

size_t telos_plugin_lock_dependency_count(const struct telos_plugin_lock *lock)
{
    return lock == NULL ? 0 : lock->dependency_count;
}

const struct telos_lock_dependency *
telos_plugin_lock_dependency_at(const struct telos_plugin_lock *lock,
                                size_t index)
{
    if (lock == NULL || index >= lock->dependency_count) {
        return NULL;
    }
    return &lock->dependencies[index];
}

static bool dependency_path(const char *base_directory,
                            const char *source,
                            char *path,
                            size_t path_size)
{
    char resolved_base[PATH_MAX];
    char candidate[PATH_MAX];
    char resolved_path[PATH_MAX];
    size_t base_size;

    if (base_directory == NULL || source == NULL || source[0] == '/' ||
        strstr(source, "://") != NULL ||
        realpath(base_directory, resolved_base) == NULL ||
        snprintf(candidate, sizeof(candidate), "%s/%s", resolved_base,
                 source) >= (int)sizeof(candidate) ||
        realpath(candidate, resolved_path) == NULL) {
        return false;
    }
    base_size = strlen(resolved_base);
    if (strncmp(resolved_path, resolved_base, base_size) != 0 ||
        (resolved_path[base_size] != '/' && resolved_path[base_size] != '\0') ||
        strlen(resolved_path) + 1 > path_size) {
        return false;
    }
    memcpy(path, resolved_path, strlen(resolved_path) + 1);
    return true;
}

bool telos_plugin_lock_verify(const struct telos_plugin_lock *lock,
                              const char *base_directory,
                              struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (lock == NULL || base_directory == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin lock and dependency directory are required");
        return false;
    }
    for (size_t index = 0; index < lock->dependency_count; ++index) {
        char path[PATH_MAX];
        char digest[65];

        if (!dependency_path(base_directory, lock->dependencies[index].source,
                             path, sizeof(path)) ||
            !telos_sha256_file(path, digest) ||
            strcmp(digest, lock->dependencies[index].sha256) != 0) {
            set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EBADMSG,
                      "Plugin dependency hash verification failed");
            return false;
        }
    }
    return true;
}

bool telos_plugin_source_digest(const char *source_directory,
                                char output[65],
                                struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (source_directory == NULL || output == NULL ||
        !telos_sha256_source_directory(source_directory, output)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin source directory could not be hashed");
        return false;
    }
    return true;
}

bool telos_plugin_lock_verify_source(const struct telos_plugin_lock *lock,
                                     const char *source_directory,
                                     struct telos_error **error)
{
    char digest[65];

    if (error != NULL) {
        *error = NULL;
    }
    if (lock == NULL || source_directory == NULL ||
        !telos_sha256_source_directory(source_directory, digest)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin lock and source directory are required");
        return false;
    }
    if (strcmp(digest, lock->source_hash) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EBADMSG,
                  "Plugin source hash verification failed");
        return false;
    }
    return true;
}

bool telos_plugin_lock_write(const struct telos_plugin_lock *lock,
                             const char *path,
                             struct telos_error **error)
{
    FILE *stream;
    bool valid = true;

    if (error != NULL) {
        *error = NULL;
    }
    if (lock == NULL || path == NULL || path[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin lock and output path are required");
        return false;
    }
    stream = fopen(path, "wb");
    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Plugin lock output could not be opened");
        return false;
    }
    valid = fprintf(stream, "format = 1\nsource_hash = \"%s\"\n",
                    lock->source_hash) >= 0;
    if (lock->dependency_count == 0) {
        valid = valid && fputs("\ndependencies = []\n", stream) >= 0;
    }
    for (size_t index = 0; valid && index < lock->dependency_count; ++index) {
        const struct telos_lock_dependency *dependency =
            &lock->dependencies[index];

        valid = fprintf(stream,
                        "\n[[dependency]]\n"
                        "name = \"%s\"\n"
                        "version = \"%s\"\n"
                        "source = \"%s\"\n"
                        "sha256 = \"%s\"\n",
                        dependency->name, dependency->version,
                        dependency->source, dependency->sha256) >= 0;
    }
    if (fclose(stream) != 0) {
        valid = false;
    }
    if (!valid) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Plugin lock output failed");
    }
    return valid;
}
