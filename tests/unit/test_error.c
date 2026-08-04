#include <stdio.h>
#include <string.h>

#include <telos/error.h>

static int test_static_error(void)
{
    struct telos_error *first =
        telos_error_static(TELOS_ERROR_DOMAIN_STATE, 9, "scope closed");
    struct telos_error *retained;
    struct telos_error *second;

    if (first == NULL ||
        telos_error_domain(first) != TELOS_ERROR_DOMAIN_STATE ||
        telos_error_code(first) != 9 ||
        strcmp(telos_error_message(first), "scope closed") != 0 ||
        telos_error_cause(first) != NULL) {
        fputs("static error metadata wrong\n", stderr);
        return 1;
    }

    /* retain/release are no-ops on the singleton. */
    retained = telos_error_retain(first);
    if (retained != first) {
        fputs("static error retain changed identity\n", stderr);
        return 1;
    }
    telos_error_release(first);
    telos_error_release(first);

    /* The singleton is overwritten by the next call. */
    second = telos_error_static(TELOS_ERROR_DOMAIN_IO, 3, "disk full");
    if (second != first ||
        telos_error_domain(second) != TELOS_ERROR_DOMAIN_IO ||
        telos_error_code(second) != 3 ||
        strcmp(telos_error_message(second), "disk full") != 0) {
        fputs("static error singleton not overwritten\n", stderr);
        return 1;
    }

    if (telos_error_static(0, 1, "x") != NULL ||
        telos_error_static(TELOS_ERROR_DOMAIN_ARGUMENT, 1, NULL) != NULL) {
        fputs("static error accepted invalid arguments\n", stderr);
        return 1;
    }
    return 0;
}

int main(void)
{
    struct telos_error *cause =
        telos_error_create(TELOS_ERROR_DOMAIN_IO, 5, "connection closed", NULL);
    struct telos_error *error;
    const struct telos_error *retained_cause;

    if (test_static_error() != 0) {
        return 1;
    }

    if (cause == NULL) {
        fputs("failed to create causal error\n", stderr);
        return 1;
    }

    error = telos_error_create(TELOS_ERROR_DOMAIN_PLUGIN, 17, "provider failed",
                               cause);
    telos_error_release(cause);

    if (error == NULL) {
        fputs("failed to create wrapping error\n", stderr);
        return 1;
    }

    retained_cause = telos_error_cause(error);
    if (telos_error_domain(error) != TELOS_ERROR_DOMAIN_PLUGIN ||
        telos_error_code(error) != 17 ||
        strcmp(telos_error_message(error), "provider failed") != 0 ||
        retained_cause == NULL ||
        telos_error_domain(retained_cause) != TELOS_ERROR_DOMAIN_IO ||
        strcmp(telos_error_message(retained_cause), "connection closed") != 0) {
        fputs("typed causal error metadata changed\n", stderr);
        telos_error_release(error);
        return 1;
    }

    telos_error_release(error);
    return 0;
}
