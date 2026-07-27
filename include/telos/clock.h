#ifndef TELOS_CLOCK_H
#define TELOS_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*telos_clock_now_fn)(
    void *context,
    int64_t *milliseconds,
    struct telos_error **error
);

struct telos_clock {
    void *context;
    telos_clock_now_fn now;
};

struct telos_clock telos_system_clock(void);

bool telos_clock_now_milliseconds(
    const struct telos_clock *clock,
    int64_t *milliseconds,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
