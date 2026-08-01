#ifndef TELOS_RESOURCE_H
#define TELOS_RESOURCE_H

#include <telos/types.h>

#include <telos/error.h>

struct telos_resource_manager;
struct telos_resource_generation;
struct telos_skill;

typedef struct telos_resource_manager telos_resource_manager;
typedef struct telos_resource_generation telos_resource_generation;
typedef struct telos_skill telos_skill;

typedef telos_resource_manager *
(*telos_resource_source_factory_fn)(const char *const *roots,
                                    size_t root_count,
                                    struct telos_error **error);

struct telos_resource_source_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_resource_source_factory_fn create;
};

telos_resource_manager *
telos_resource_manager_create(const char *const *skill_roots,
                              size_t skill_root_count,
                              struct telos_error **error);

void telos_resource_manager_destroy(telos_resource_manager *manager);

bool telos_resource_manager_reload(telos_resource_manager *manager,
                                   struct telos_error **error);

telos_resource_generation *
telos_resource_manager_acquire(telos_resource_manager *manager);

telos_resource_generation *
telos_resource_generation_retain(const telos_resource_generation *generation);

void
telos_resource_generation_release(const telos_resource_generation *generation);

uint64_t
telos_resource_generation_number(const telos_resource_generation *generation);

size_t
telos_resource_generation_skill_count(const telos_resource_generation *gen);

const telos_skill *
telos_resource_generation_skill_at(const telos_resource_generation *gen,
                                   size_t index);

const telos_skill *
telos_resource_generation_select_skill(const telos_resource_generation *gen,
                                       const char *request,
                                       struct telos_error **error);

const char *telos_skill_name(const telos_skill *skill);

const char *telos_skill_description(const telos_skill *skill);

const char *telos_skill_instructions(const telos_skill *skill);

char *telos_skill_resolve_path(const telos_skill *skill,
                               const char *relative_path,
                               struct telos_error **error);

char *telos_skill_resolve_script(const telos_skill *skill,
                                 const char *relative_path,
                                 const char *const *available_capabilities,
                                 size_t available_capability_count,
                                 struct telos_error **error);

void telos_resource_string_free(char *value);

#endif
