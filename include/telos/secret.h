#ifndef TELOS_SECRET_H
#define TELOS_SECRET_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_secret_reference;
struct telos_secret_broker;
struct telos_secret_material;

typedef char *(*telos_secret_resolve_fn)(
    const char *reference,
    const char *target,
    void *context,
    struct telos_error **error
);

struct telos_secret_reference *telos_secret_reference_create(
    const char *reference,
    struct telos_error **error
);

void telos_secret_reference_destroy(
    struct telos_secret_reference *reference
);

const char *telos_secret_reference_id(
    const struct telos_secret_reference *reference
);

struct telos_secret_broker *telos_secret_broker_create(
    telos_secret_resolve_fn resolve,
    void *resolve_context,
    struct telos_error **error
);

void telos_secret_broker_destroy(struct telos_secret_broker *broker);

struct telos_secret_material *telos_secret_broker_resolve(
    struct telos_secret_broker *broker,
    const struct telos_secret_reference *reference,
    const char *target,
    const char *const *capabilities,
    size_t capability_count,
    bool trusted_boundary,
    struct telos_error **error
);

const char *telos_secret_material_data(
    const struct telos_secret_material *material
);

void telos_secret_material_destroy(struct telos_secret_material *material);

#ifdef __cplusplus
}
#endif

#endif
