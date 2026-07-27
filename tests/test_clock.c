#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/clock.h>

static bool fixed_now(
    void *context,
    int64_t *milliseconds,
    struct telos_error **error
)
{
    (void)error;
    *milliseconds = *(const int64_t *)context;
    return true;
}

int main(void)
{
    int64_t fixed = INT64_C(1722168000123);
    int64_t actual = 0;
    struct telos_clock clock = {
        .context = &fixed,
        .now = fixed_now,
    };
    struct telos_error *error = NULL;

    assert(telos_clock_now_milliseconds(&clock, &actual, &error));
    assert(actual == fixed);
    assert(error == NULL);

    assert(!telos_clock_now_milliseconds(NULL, &actual, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    assert(telos_error_code(error) == EINVAL);
    telos_error_release(error);

    clock = telos_system_clock();
    assert(telos_clock_now_milliseconds(&clock, &actual, &error));
    assert(actual > 0);
    assert(error == NULL);
    return 0;
}
