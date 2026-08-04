#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/error.h>

struct telos_error {
    atomic_uint references;
    enum telos_error_domain domain;
    int code;
    struct telos_error *cause;
    char message[];
};

struct telos_error *telos_error_create(enum telos_error_domain domain,
                                       int code,
                                       const char *message,
                                       const struct telos_error *cause)
{
    struct telos_error *error;
    size_t message_size;

    if (domain < TELOS_ERROR_DOMAIN_ARGUMENT ||
        domain > TELOS_ERROR_DOMAIN_PLUGIN || message == NULL) {
        return NULL;
    }

    message_size = strlen(message) + 1;
    if (message_size > SIZE_MAX - sizeof(*error)) {
        return NULL;
    }

    error = malloc(sizeof(*error) + message_size);
    if (error == NULL) {
        return NULL;
    }

    atomic_init(&error->references, 1);
    error->domain = domain;
    error->code = code;
    error->cause = telos_error_retain(cause);
    memcpy(error->message, message, message_size);
    return error;
}

struct telos_error *telos_error_retain(const struct telos_error *error)
{
    struct telos_error *mutable_error = (struct telos_error *)error;

    if (mutable_error != NULL) {
        atomic_fetch_add_explicit(&mutable_error->references, 1,
                                  memory_order_relaxed);
    }

    return mutable_error;
}

void telos_error_release(const struct telos_error *error)
{
    struct telos_error *mutable_error = (struct telos_error *)error;

    if (mutable_error == NULL) {
        return;
    }

    if (atomic_fetch_sub_explicit(&mutable_error->references, 1,
                                  memory_order_acq_rel) == 1) {
        telos_error_release(mutable_error->cause);
        free(mutable_error);
    }
}

enum telos_error_domain telos_error_domain(const struct telos_error *error)
{
    if (error == NULL) {
        return 0;
    }

    return error->domain;
}

int telos_error_code(const struct telos_error *error)
{
    if (error == NULL) {
        return 0;
    }

    return error->code;
}

const char *telos_error_message(const struct telos_error *error)
{
    if (error == NULL) {
        return "";
    }

    return error->message;
}

const struct telos_error *telos_error_cause(const struct telos_error *error)
{
    if (error == NULL) {
        return NULL;
    }

    return error->cause;
}

void telos_error_set(struct telos_error **error,
                     enum telos_error_domain domain,
                     int code,
                     const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

