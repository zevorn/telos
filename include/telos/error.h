#ifndef TELOS_ERROR_H
#define TELOS_ERROR_H

enum telos_error_domain {
    TELOS_ERROR_DOMAIN_ARGUMENT = 1,
    TELOS_ERROR_DOMAIN_MEMORY,
    TELOS_ERROR_DOMAIN_STATE,
    TELOS_ERROR_DOMAIN_IO,
    TELOS_ERROR_DOMAIN_PROTOCOL,
    TELOS_ERROR_DOMAIN_PERMISSION,
    TELOS_ERROR_DOMAIN_TIMEOUT,
    TELOS_ERROR_DOMAIN_CANCELLED,
    TELOS_ERROR_DOMAIN_PLUGIN,
};

struct telos_error;

struct telos_error *telos_error_create(enum telos_error_domain domain,
                                       int code,
                                       const char *message,
                                       const struct telos_error *cause);

/*
 * No-heap error factory for no-heap paths (see telos/no_heap.h).
 *
 * Returns a process-wide singleton error; message must point to
 * static storage (string literals are fine).  retain/release are
 * no-ops on it, so it must not be held beyond the caller's use.
 * The singleton is overwritten on every call: do not keep two
 * static errors alive at once.  Intended for single-threaded
 * critical paths only.
 */
struct telos_error *telos_error_static(enum telos_error_domain domain,
                                       int code,
                                       const char *message);

struct telos_error *telos_error_retain(const struct telos_error *error);

void telos_error_release(const struct telos_error *error);

enum telos_error_domain telos_error_domain(const struct telos_error *error);

int telos_error_code(const struct telos_error *error);

const char *telos_error_message(const struct telos_error *error);

const struct telos_error *telos_error_cause(const struct telos_error *error);


/*
 * Conditionally create and assign an error.  Does nothing when error is
 * NULL or already holds an error (preserves the first error).
 */
void telos_error_set(struct telos_error **error,
                     enum telos_error_domain domain,
                     int code,
                     const char *message);

#endif
