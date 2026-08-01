#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/secret.h>

struct telos_secret_reference {
    char *id;
};

struct telos_secret_broker {
    telos_secret_resolve_fn resolve;
    void *resolve_context;
};

struct telos_secret_material {
    char *data;
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

static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

struct telos_secret_reference *
telos_secret_reference_create(const char *reference, struct telos_error **error)
{
    struct telos_secret_reference *result;

    if (error != NULL) {
        *error = NULL;
    }
    if (reference == NULL || strncmp(reference, "secret:", 7) != 0 ||
        reference[7] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Secret Reference must use the secret: scheme");
        return NULL;
    }
    result = calloc(1, sizeof(*result));
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Secret Reference allocation failed");
        return NULL;
    }
    result->id = copy_string(reference);
    if (result->id == NULL) {
        free(result);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Secret Reference ID allocation failed");
        return NULL;
    }
    return result;
}

void telos_secret_reference_destroy(struct telos_secret_reference *reference)
{
    if (reference != NULL) {
        free(reference->id);
        free(reference);
    }
}

const char *
telos_secret_reference_id(const struct telos_secret_reference *reference)
{
    return reference == NULL ? NULL : reference->id;
}

struct telos_secret_broker *
telos_secret_broker_create(telos_secret_resolve_fn resolve,
                           void *resolve_context,
                           struct telos_error **error)
{
    struct telos_secret_broker *broker;

    if (error != NULL) {
        *error = NULL;
    }
    if (resolve == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Secret resolver is required");
        return NULL;
    }
    broker = calloc(1, sizeof(*broker));
    if (broker == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Secret Broker allocation failed");
        return NULL;
    }
    broker->resolve = resolve;
    broker->resolve_context = resolve_context;
    return broker;
}

void telos_secret_broker_destroy(struct telos_secret_broker *broker)
{
    free(broker);
}

static bool has_capability(const char *const *capabilities,
                           size_t count,
                           const char *required)
{
    for (size_t index = 0; index < count; ++index) {
        if (capabilities[index] != NULL &&
            strcmp(capabilities[index], required) == 0) {
            return true;
        }
    }
    return false;
}

struct telos_secret_material *
telos_secret_broker_resolve(struct telos_secret_broker *broker,
                            const struct telos_secret_reference *reference,
                            const char *target,
                            const char *const *capabilities,
                            size_t capability_count,
                            bool trusted_boundary,
                            struct telos_error **error)
{
    const char *reference_target;
    char *required;
    size_t required_size;
    struct telos_secret_material *material;

    if (error != NULL) {
        *error = NULL;
    }
    if (broker == NULL || reference == NULL || target == NULL ||
        (capability_count > 0 && capabilities == NULL)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Secret resolution arguments are invalid");
        return NULL;
    }
    if (!trusted_boundary) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Secrets may only resolve at a trusted boundary");
        return NULL;
    }
    reference_target = reference->id + 7;
    if (strcmp(reference_target, target) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Secret Reference is not authorized for this target");
        return NULL;
    }
    required_size = strlen("secret.use:") + strlen(reference_target) + 1;
    required = malloc(required_size);
    if (required == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Secret Capability allocation failed");
        return NULL;
    }
    memcpy(required, "secret.use:", strlen("secret.use:"));
    strcpy(required + strlen("secret.use:"), reference_target);
    if (!has_capability(capabilities, capability_count, required)) {
        free(required);
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Secret use Capability is missing");
        return NULL;
    }
    free(required);

    material = calloc(1, sizeof(*material));
    if (material == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Secret material allocation failed");
        return NULL;
    }
    material->data =
        broker->resolve(reference->id, target, broker->resolve_context, error);
    if (material->data == NULL) {
        free(material);
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, ENOENT,
                      "Secret resolver returned no material");
        }
        return NULL;
    }
    return material;
}

const char *
telos_secret_material_data(const struct telos_secret_material *material)
{
    return material == NULL ? NULL : material->data;
}

void telos_secret_material_destroy(struct telos_secret_material *material)
{
    if (material != NULL) {
        if (material->data != NULL) {
            /* Volatile prevents the compiler from eliding the secret wipe. */
            volatile char *cursor = (volatile char *)material->data;
            size_t size = strlen(material->data);

            for (size_t index = 0; index < size; ++index) {
                cursor[index] = 0;
            }
        }
        free(material->data);
        free(material);
    }
}
