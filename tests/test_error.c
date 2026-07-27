#include <stdio.h>
#include <string.h>

#include <telos/error.h>

int main(void)
{
    struct telos_error *cause = telos_error_create(
        TELOS_ERROR_DOMAIN_IO,
        5,
        "connection closed",
        NULL
    );
    struct telos_error *error;
    const struct telos_error *retained_cause;

    if (cause == NULL) {
        fputs("failed to create causal error\n", stderr);
        return 1;
    }

    error = telos_error_create(
        TELOS_ERROR_DOMAIN_PLUGIN,
        17,
        "provider failed",
        cause
    );
    telos_error_release(cause);

    if (error == NULL) {
        fputs("failed to create wrapping error\n", stderr);
        return 1;
    }

    retained_cause = telos_error_cause(error);
    if (
        telos_error_domain(error) != TELOS_ERROR_DOMAIN_PLUGIN
        || telos_error_code(error) != 17
        || strcmp(telos_error_message(error), "provider failed") != 0
        || retained_cause == NULL
        || telos_error_domain(retained_cause) != TELOS_ERROR_DOMAIN_IO
        || strcmp(telos_error_message(retained_cause), "connection closed") != 0
    ) {
        fputs("typed causal error metadata changed\n", stderr);
        telos_error_release(error);
        return 1;
    }

    telos_error_release(error);
    return 0;
}
