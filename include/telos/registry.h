#ifndef TELOS_REGISTRY_H
#define TELOS_REGISTRY_H

#include <telos/types.h>

#include <telos/error.h>

enum telos_extension_kind {
    TELOS_EXTENSION_PROVIDER = 1,
    TELOS_EXTENSION_TOOL,
    TELOS_EXTENSION_POLICY,
    TELOS_EXTENSION_CONTEXT_SOURCE,
    TELOS_EXTENSION_STORE,
    TELOS_EXTENSION_WORKFLOW_STEP,
    TELOS_EXTENSION_TRANSPORT,
    TELOS_EXTENSION_CODEC,
    TELOS_EXTENSION_FRONTEND,
    TELOS_EXTENSION_BUILDER,
    TELOS_EXTENSION_STATE_FRAGMENT,
    TELOS_EXTENSION_EVENT_HANDLER,
    TELOS_EXTENSION_PROMPT,
    TELOS_EXTENSION_AUTHENTICATION,
};

struct telos_extension_descriptor {
    const char *id;
    const char *plugin_id;
    enum telos_extension_kind kind;
    const char *const *required_capabilities;
    size_t required_capability_count;
    const void *implementation;
};

typedef struct telos_extension_descriptor telos_extension_descriptor;

struct telos_registry;
struct telos_registry_transaction;
struct telos_registry_generation;

typedef struct telos_registry telos_registry;
typedef struct telos_registry_generation telos_registry_generation;
typedef struct telos_registry_transaction telos_registry_transaction;

telos_registry *telos_registry_create(const char *const *capabilities,
                                      size_t capability_count,
                                      struct telos_error **error);

void telos_registry_destroy(telos_registry *registry);

telos_registry_generation *telos_registry_acquire(telos_registry *registry);

telos_registry_generation *
telos_registry_generation_retain(const telos_registry_generation *generation);

void
telos_registry_generation_release(const telos_registry_generation *generation);

uint64_t
telos_registry_generation_number(const telos_registry_generation *generation);

size_t
telos_registry_generation_count(const telos_registry_generation *generation);

const telos_extension_descriptor *
telos_registry_generation_at(const telos_registry_generation *generation,
                             size_t index);

const telos_extension_descriptor *
telos_registry_generation_find(const telos_registry_generation *generation,
                               enum telos_extension_kind kind,
                               const char *id);

telos_registry_transaction *
telos_registry_transaction_begin(telos_registry *registry,
                                 const char *plugin_id,
                                 struct telos_error **error);

bool
telos_registry_transaction_add(telos_registry_transaction *transaction,
                               const telos_extension_descriptor *descriptor,
                               struct telos_error **error);

bool telos_registry_transaction_commit(telos_registry_transaction *transaction,
                                       struct telos_error **error);

void telos_registry_transaction_abort(telos_registry_transaction *transaction);

#endif
