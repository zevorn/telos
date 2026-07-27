#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/resource.h>

#define TELOS_SKILL_MAX_SIZE (1024U * 1024U)

struct telos_skill {
    char *name;
    char *description;
    char *instructions;
    char *directory;
    bool has_openai_metadata;
};

struct telos_resource_generation {
    atomic_uint references;
    uint64_t number;
    size_t skill_count;
    struct telos_skill *skills;
};

struct telos_resource_manager {
    pthread_mutex_t mutex;
    size_t root_count;
    char **roots;
    struct telos_resource_generation *current;
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

static char *copy_range(const char *start, size_t size)
{
    char *copy;

    if (size == SIZE_MAX) {
        return NULL;
    }
    copy = calloc(size + 1, 1);
    if (copy != NULL) {
        memcpy(copy, start, size);
    }
    return copy;
}

static char *copy_string(const char *value)
{
    return value == NULL ? NULL : copy_range(value, strlen(value));
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

static char *read_file(
    const char *path,
    size_t maximum_size,
    struct telos_error **error
)
{
    FILE *file = fopen(path, "rb");
    long length = -1;
    char *content;

    if (file == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Resource file could not be opened"
        );
        return NULL;
    }
    if (
        fseek(file, 0, SEEK_END) != 0
        || (length = ftell(file)) < 0
        || (size_t)length > maximum_size
        || fseek(file, 0, SEEK_SET) != 0
    ) {
        fclose(file);
        set_error(
            error,
            (length >= 0 && (size_t)length > maximum_size)
                ? TELOS_ERROR_DOMAIN_ARGUMENT
                : TELOS_ERROR_DOMAIN_IO,
            (length >= 0 && (size_t)length > maximum_size) ? EFBIG : EIO,
            "Resource file size is invalid"
        );
        return NULL;
    }
    content = calloc((size_t)length + 1, 1);
    if (
        content == NULL
        || fread(content, 1, (size_t)length, file) != (size_t)length
    ) {
        free(content);
        fclose(file);
        set_error(
            error,
            content == NULL
                ? TELOS_ERROR_DOMAIN_MEMORY
                : TELOS_ERROR_DOMAIN_IO,
            content == NULL ? ENOMEM : EIO,
            "Resource file could not be read"
        );
        return NULL;
    }
    fclose(file);
    return content;
}

static char *frontmatter_value(
    const char *frontmatter,
    size_t size,
    const char *key
)
{
    const size_t key_size = strlen(key);
    const char *cursor = frontmatter;
    const char *end = frontmatter + size;

    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        const char *value;
        const char *value_end;

        if (line_end == NULL) {
            line_end = end;
        }
        if (
            (size_t)(line_end - cursor) > key_size
            && memcmp(cursor, key, key_size) == 0
            && cursor[key_size] == ':'
        ) {
            value = cursor + key_size + 1;
            while (value < line_end && isspace((unsigned char)*value)) {
                ++value;
            }
            value_end = line_end;
            while (
                value_end > value
                && isspace((unsigned char)value_end[-1])
            ) {
                --value_end;
            }
            if (
                value_end - value >= 2
                && (
                    (*value == '"' && value_end[-1] == '"')
                    || (*value == '\'' && value_end[-1] == '\'')
                )
            ) {
                ++value;
                --value_end;
            }
            return value == value_end
                ? NULL
                : copy_range(value, (size_t)(value_end - value));
        }
        cursor = line_end < end ? line_end + 1 : end;
    }
    return NULL;
}

static void skill_clear(struct telos_skill *skill)
{
    free(skill->directory);
    free(skill->instructions);
    free(skill->description);
    free(skill->name);
    memset(skill, 0, sizeof(*skill));
}

static bool file_exists(const char *path)
{
    struct stat status;

    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static bool load_skill(
    const char *directory,
    struct telos_skill *skill,
    struct telos_error **error
)
{
    char *path = join_path(directory, "SKILL.md");
    char *content;
    char *frontmatter_end;
    const char *body;
    char *agents_directory;
    char *metadata_path;

    memset(skill, 0, sizeof(*skill));
    if (path == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Skill path allocation failed"
        );
        return false;
    }
    content = read_file(path, TELOS_SKILL_MAX_SIZE, error);
    free(path);
    if (content == NULL) {
        return false;
    }
    if (strncmp(content, "---\n", 4) != 0) {
        free(content);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "SKILL.md must start with YAML frontmatter"
        );
        return false;
    }
    frontmatter_end = strstr(content + 4, "\n---\n");
    if (frontmatter_end == NULL) {
        free(content);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "SKILL.md frontmatter is not terminated"
        );
        return false;
    }
    skill->name = frontmatter_value(
        content + 4,
        (size_t)(frontmatter_end - (content + 4)),
        "name"
    );
    skill->description = frontmatter_value(
        content + 4,
        (size_t)(frontmatter_end - (content + 4)),
        "description"
    );
    body = frontmatter_end + 5;
    skill->instructions = copy_string(body);
    skill->directory = realpath(directory, NULL);
    free(content);
    if (
        skill->name == NULL
        || skill->description == NULL
        || skill->instructions == NULL
        || skill->directory == NULL
    ) {
        skill_clear(skill);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "SKILL.md requires name, description, and readable instructions"
        );
        return false;
    }

    agents_directory = join_path(directory, "agents");
    metadata_path = agents_directory == NULL
        ? NULL
        : join_path(agents_directory, "openai.yaml");
    skill->has_openai_metadata =
        metadata_path != NULL && file_exists(metadata_path);
    free(metadata_path);
    free(agents_directory);
    return true;
}

static void generation_destroy(struct telos_resource_generation *generation)
{
    for (size_t index = 0; index < generation->skill_count; ++index) {
        skill_clear(&generation->skills[index]);
    }
    free(generation->skills);
    free(generation);
}

static int compare_strings(const void *left, const void *right)
{
    const char *const *lhs = left;
    const char *const *rhs = right;

    return strcmp(*lhs, *rhs);
}

static void free_names(char **names, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        free(names[index]);
    }
    free(names);
}

static bool list_directories(
    const char *root,
    char ***names,
    size_t *count,
    struct telos_error **error
)
{
    DIR *directory = opendir(root);
    size_t capacity = 0;
    struct dirent *entry;

    *names = NULL;
    *count = 0;
    if (directory == NULL) {
        if (errno == ENOENT) {
            return true;
        }
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Skill root could not be opened"
        );
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char *path;
        struct stat status;
        char **new_names;

        if (
            strcmp(entry->d_name, ".") == 0
            || strcmp(entry->d_name, "..") == 0
        ) {
            continue;
        }
        path = join_path(root, entry->d_name);
        if (path == NULL) {
            closedir(directory);
            free_names(*names, *count);
            set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Skill directory path allocation failed"
            );
            return false;
        }
        if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
            free(path);
            continue;
        }
        free(path);

        if (*count == capacity) {
            size_t new_capacity = capacity == 0 ? 8 : capacity * 2;

            if (
                new_capacity < capacity
                || new_capacity > SIZE_MAX / sizeof(*new_names)
            ) {
                closedir(directory);
                free_names(*names, *count);
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Skill directory list overflow"
                );
                return false;
            }
            new_names = realloc(
                *names,
                new_capacity * sizeof(*new_names)
            );
            if (new_names == NULL) {
                closedir(directory);
                free_names(*names, *count);
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Skill directory list allocation failed"
                );
                return false;
            }
            *names = new_names;
            capacity = new_capacity;
        }
        (*names)[*count] = copy_string(entry->d_name);
        if ((*names)[*count] == NULL) {
            closedir(directory);
            free_names(*names, *count);
            set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Skill directory name allocation failed"
            );
            return false;
        }
        *count += 1;
    }
    closedir(directory);
    if (*count > 1) {
        qsort(*names, *count, sizeof(**names), compare_strings);
    }
    return true;
}

static bool append_skill(
    struct telos_resource_generation *generation,
    const char *directory,
    struct telos_error **error
)
{
    struct telos_skill *skills;

    if (
        generation->skill_count == SIZE_MAX
        || generation->skill_count + 1
            > SIZE_MAX / sizeof(*generation->skills)
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Skill Generation size overflow"
        );
        return false;
    }
    skills = realloc(
        generation->skills,
        (generation->skill_count + 1) * sizeof(*skills)
    );
    if (skills == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Skill Generation allocation failed"
        );
        return false;
    }
    generation->skills = skills;
    if (!load_skill(
        directory,
        &generation->skills[generation->skill_count],
        error
    )) {
        return false;
    }
    generation->skill_count += 1;
    return true;
}

static struct telos_resource_generation *scan_generation(
    struct telos_resource_manager *manager,
    uint64_t number,
    struct telos_error **error
)
{
    struct telos_resource_generation *generation =
        calloc(1, sizeof(*generation));

    if (generation == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Resource Generation allocation failed"
        );
        return NULL;
    }
    atomic_init(&generation->references, 1);
    generation->number = number;

    for (size_t root_index = 0; root_index < manager->root_count; ++root_index) {
        char **names;
        size_t count;

        if (!list_directories(
            manager->roots[root_index],
            &names,
            &count,
            error
        )) {
            telos_resource_generation_release(generation);
            return NULL;
        }
        for (size_t index = 0; index < count; ++index) {
            char *directory = join_path(manager->roots[root_index], names[index]);
            char *skill_path = directory == NULL
                ? NULL
                : join_path(directory, "SKILL.md");

            if (directory == NULL || skill_path == NULL) {
                free(skill_path);
                free(directory);
                free_names(names, count);
                telos_resource_generation_release(generation);
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Skill scan path allocation failed"
                );
                return NULL;
            }
            if (
                file_exists(skill_path)
                && !append_skill(generation, directory, error)
            ) {
                free(skill_path);
                free(directory);
                free_names(names, count);
                telos_resource_generation_release(generation);
                return NULL;
            }
            free(skill_path);
            free(directory);
        }
        free_names(names, count);
    }
    return generation;
}

struct telos_resource_manager *telos_resource_manager_create(
    const char *const *skill_roots,
    size_t skill_root_count,
    struct telos_error **error
)
{
    struct telos_resource_manager *manager;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    if (skill_root_count > 0 && skill_roots == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Skill roots are invalid"
        );
        return NULL;
    }
    for (size_t index = 0; index < skill_root_count; ++index) {
        if (skill_roots[index] == NULL || skill_roots[index][0] == '\0') {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_ARGUMENT,
                EINVAL,
                "Skill root paths must not be empty"
            );
            return NULL;
        }
    }
    manager = calloc(1, sizeof(*manager));
    if (manager == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Resource Manager allocation failed"
        );
        return NULL;
    }
    result = pthread_mutex_init(&manager->mutex, NULL);
    if (result != 0) {
        free(manager);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            result,
            "Resource Manager mutex initialization failed"
        );
        return NULL;
    }
    if (skill_root_count > 0) {
        manager->roots = calloc(skill_root_count, sizeof(*manager->roots));
        if (manager->roots == NULL) {
            telos_resource_manager_destroy(manager);
            set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Skill root allocation failed"
            );
            return NULL;
        }
        for (size_t index = 0; index < skill_root_count; ++index) {
            manager->roots[index] = copy_string(skill_roots[index]);
            if (manager->roots[index] == NULL) {
                manager->root_count = index;
                telos_resource_manager_destroy(manager);
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Skill root copy failed"
                );
                return NULL;
            }
            manager->root_count = index + 1;
        }
    }
    manager->current = scan_generation(manager, 1, error);
    if (manager->current == NULL) {
        telos_resource_manager_destroy(manager);
        return NULL;
    }
    return manager;
}

void telos_resource_manager_destroy(struct telos_resource_manager *manager)
{
    if (manager == NULL) {
        return;
    }
    telos_resource_generation_release(manager->current);
    for (size_t index = 0; index < manager->root_count; ++index) {
        free(manager->roots[index]);
    }
    free(manager->roots);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

bool telos_resource_manager_reload(
    struct telos_resource_manager *manager,
    struct telos_error **error
)
{
    struct telos_resource_generation *next;
    struct telos_resource_generation *prior;
    uint64_t number;

    if (error != NULL) {
        *error = NULL;
    }
    if (manager == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Resource Manager is required"
        );
        return false;
    }
    pthread_mutex_lock(&manager->mutex);
    number = manager->current->number + 1;
    pthread_mutex_unlock(&manager->mutex);
    next = scan_generation(manager, number, error);
    if (next == NULL) {
        return false;
    }
    pthread_mutex_lock(&manager->mutex);
    prior = manager->current;
    manager->current = next;
    pthread_mutex_unlock(&manager->mutex);
    telos_resource_generation_release(prior);
    return true;
}

struct telos_resource_generation *telos_resource_manager_acquire(
    struct telos_resource_manager *manager
)
{
    struct telos_resource_generation *generation;

    if (manager == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&manager->mutex);
    generation = telos_resource_generation_retain(manager->current);
    pthread_mutex_unlock(&manager->mutex);
    return generation;
}

struct telos_resource_generation *telos_resource_generation_retain(
    const struct telos_resource_generation *generation
)
{
    struct telos_resource_generation *mutable_generation =
        (struct telos_resource_generation *)generation;

    if (mutable_generation != NULL) {
        atomic_fetch_add_explicit(
            &mutable_generation->references,
            1,
            memory_order_relaxed
        );
    }
    return mutable_generation;
}

void telos_resource_generation_release(
    const struct telos_resource_generation *generation
)
{
    struct telos_resource_generation *mutable_generation =
        (struct telos_resource_generation *)generation;

    if (
        mutable_generation != NULL
        && atomic_fetch_sub_explicit(
            &mutable_generation->references,
            1,
            memory_order_acq_rel
        ) == 1
    ) {
        generation_destroy(mutable_generation);
    }
}

uint64_t telos_resource_generation_number(
    const struct telos_resource_generation *generation
)
{
    return generation == NULL ? 0 : generation->number;
}

size_t telos_resource_generation_skill_count(
    const struct telos_resource_generation *generation
)
{
    return generation == NULL ? 0 : generation->skill_count;
}

const struct telos_skill *telos_resource_generation_skill_at(
    const struct telos_resource_generation *generation,
    size_t index
)
{
    if (generation == NULL || index >= generation->skill_count) {
        return NULL;
    }
    return &generation->skills[index];
}

static bool contains_case_insensitive(
    const char *haystack,
    const char *needle
)
{
    size_t needle_size = strlen(needle);

    if (needle_size == 0) {
        return false;
    }
    for (; *haystack != '\0'; ++haystack) {
        size_t index = 0;

        while (
            index < needle_size
            && haystack[index] != '\0'
            && tolower((unsigned char)haystack[index])
                == tolower((unsigned char)needle[index])
        ) {
            ++index;
        }
        if (index == needle_size) {
            return true;
        }
    }
    return false;
}

const struct telos_skill *telos_resource_generation_select_skill(
    const struct telos_resource_generation *generation,
    const char *request,
    struct telos_error **error
)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (generation == NULL || request == NULL || request[0] == '\0') {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Skill selection request is invalid"
        );
        return NULL;
    }
    if (request[0] == '$') {
        const char *end = request + 1;

        while (*end != '\0' && !isspace((unsigned char)*end)) {
            ++end;
        }
        for (size_t index = 0; index < generation->skill_count; ++index) {
            const struct telos_skill *skill = &generation->skills[index];

            if (
                strlen(skill->name) == (size_t)(end - request - 1)
                && memcmp(
                    skill->name,
                    request + 1,
                    (size_t)(end - request - 1)
                ) == 0
            ) {
                return skill;
            }
        }
    } else {
        for (size_t index = 0; index < generation->skill_count; ++index) {
            const struct telos_skill *skill = &generation->skills[index];

            if (
                contains_case_insensitive(request, skill->name)
                || contains_case_insensitive(
                    request,
                    skill->description
                )
            ) {
                return skill;
            }
        }
    }
    set_error(
        error,
        TELOS_ERROR_DOMAIN_ARGUMENT,
        ENOENT,
        "No Skill matched the request"
    );
    return NULL;
}

const char *telos_skill_name(const struct telos_skill *skill)
{
    return skill == NULL ? NULL : skill->name;
}

const char *telos_skill_description(const struct telos_skill *skill)
{
    return skill == NULL ? NULL : skill->description;
}

const char *telos_skill_instructions(const struct telos_skill *skill)
{
    return skill == NULL ? NULL : skill->instructions;
}

bool telos_skill_has_openai_metadata(const struct telos_skill *skill)
{
    return skill != NULL && skill->has_openai_metadata;
}

static bool relative_path_safe(const char *path)
{
    const char *cursor = path;

    if (
        path == NULL
        || path[0] == '\0'
        || path[0] == '/'
        || path[strlen(path) - 1] == '/'
    ) {
        return false;
    }
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '/');
        size_t size = end == NULL
            ? strlen(cursor)
            : (size_t)(end - cursor);

        if (
            size == 0
            || (size == 1 && cursor[0] == '.')
            || (size == 2 && cursor[0] == '.' && cursor[1] == '.')
        ) {
            return false;
        }
        cursor = end == NULL ? cursor + size : end + 1;
    }
    return true;
}

char *telos_skill_resolve_path(
    const struct telos_skill *skill,
    const char *relative_path,
    struct telos_error **error
)
{
    char *joined;
    char *resolved;
    size_t directory_size;

    if (error != NULL) {
        *error = NULL;
    }
    if (skill == NULL || !relative_path_safe(relative_path)) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PERMISSION,
            EPERM,
            "Skill path must remain inside the Skill directory"
        );
        return NULL;
    }
    joined = join_path(skill->directory, relative_path);
    if (joined == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Skill path allocation failed"
        );
        return NULL;
    }
    resolved = realpath(joined, NULL);
    free(joined);
    if (resolved == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Skill path does not exist"
        );
        return NULL;
    }
    directory_size = strlen(skill->directory);
    if (
        strncmp(resolved, skill->directory, directory_size) != 0
        || resolved[directory_size] != '/'
    ) {
        free(resolved);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PERMISSION,
            EPERM,
            "Skill path escaped the Skill directory"
        );
        return NULL;
    }
    return resolved;
}

char *telos_skill_resolve_script(
    const struct telos_skill *skill,
    const char *relative_path,
    const char *const *available_capabilities,
    size_t available_capability_count,
    struct telos_error **error
)
{
    bool can_spawn = false;
    char *resolved;
    struct stat status;

    if (error != NULL) {
        *error = NULL;
    }
    if (
        available_capability_count > 0
        && available_capabilities == NULL
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Script Capability list is invalid"
        );
        return NULL;
    }
    for (size_t index = 0; index < available_capability_count; ++index) {
        if (
            available_capabilities[index] != NULL
            && strcmp(
                available_capabilities[index],
                "process.spawn"
            ) == 0
        ) {
            can_spawn = true;
            break;
        }
    }
    if (
        !can_spawn
        || relative_path == NULL
        || strncmp(relative_path, "scripts/", 8) != 0
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PERMISSION,
            EACCES,
            "Skill script requires the process.spawn Capability"
        );
        return NULL;
    }
    resolved = telos_skill_resolve_path(skill, relative_path, error);
    if (resolved == NULL) {
        return NULL;
    }
    if (
        stat(resolved, &status) != 0
        || !S_ISREG(status.st_mode)
        || access(resolved, X_OK) != 0
    ) {
        free(resolved);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PERMISSION,
            EACCES,
            "Skill script is not executable"
        );
        return NULL;
    }
    return resolved;
}

void telos_resource_string_free(char *value)
{
    free(value);
}
