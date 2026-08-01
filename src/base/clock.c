#include <errno.h>
#include <stddef.h>
#include <time.h>

#include <telos/clock.h>

static bool
system_now(void *context, int64_t *milliseconds, struct telos_error **error)
{
    struct timespec current;

    (void)context;
    if (timespec_get(&current, TIME_UTC) != TIME_UTC) {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_IO, EIO,
                                        "System clock read failed", NULL);
        }
        return false;
    }
    *milliseconds = (int64_t)current.tv_sec * INT64_C(1000) +
                    current.tv_nsec / INT64_C(1000000);
    return true;
}

struct telos_clock telos_system_clock(void)
{
    return (struct telos_clock){
        .context = NULL,
        .now = system_now,
    };
}

bool telos_clock_now_milliseconds(const struct telos_clock *clock,
                                  int64_t *milliseconds,
                                  struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (clock == NULL || clock->now == NULL || milliseconds == NULL) {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                                        "Clock and output are required", NULL);
        }
        return false;
    }
    return clock->now(clock->context, milliseconds, error);
}
