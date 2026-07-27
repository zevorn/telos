#ifndef TELOS_RESOURCE_H
#define TELOS_RESOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_resource_manager;
struct telos_resource_generation;
struct telos_skill;

struct telos_resource_manager *telos_resource_manager_create(
    const char *const *skill_roots,
    size_t skill_root_count,
    struct telos_error **error
);

void telos_resource_manager_destroy(struct telos_resource_manager *manager);

bool telos_resource_manager_reload(
    struct telos_resource_manager *manager,
    struct telos_error **error
);

struct telos_resource_generation *telos_resource_manager_acquire(
    struct telos_resource_manager *manager
);

struct telos_resource_generation *telos_resource_generation_retain(
    const struct telos_resource_generation *generation
);

void telos_resource_generation_release(
    const struct telos_resource_generation *generation
);

uint64_t telos_resource_generation_number(
    const struct telos_resource_generation *generation
);

size_t telos_resource_generation_skill_count(
    const struct telos_resource_generation *generation
);

const struct telos_skill *telos_resource_generation_skill_at(
    const struct telos_resource_generation *generation,
    size_t index
);

const struct telos_skill *telos_resource_generation_select_skill(
    const struct telos_resource_generation *generation,
    const char *request,
    struct telos_error **error
);

const char *telos_skill_name(const struct telos_skill *skill);

const char *telos_skill_description(const struct telos_skill *skill);

const char *telos_skill_instructions(const struct telos_skill *skill);

bool telos_skill_has_openai_metadata(const struct telos_skill *skill);

char *telos_skill_resolve_path(
    const struct telos_skill *skill,
    const char *relative_path,
    struct telos_error **error
);

char *telos_skill_resolve_script(
    const struct telos_skill *skill,
    const char *relative_path,
    const char *const *available_capabilities,
    size_t available_capability_count,
    struct telos_error **error
);

void telos_resource_string_free(char *value);

#ifdef __cplusplus
}
#endif

#endif
