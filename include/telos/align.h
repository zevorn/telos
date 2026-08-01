#ifndef TELOS_ALIGN_H
#define TELOS_ALIGN_H

#include <telos/types.h>

#include <telos/checked_math.h>

static inline bool telos_is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static inline bool
telos_align_down(size_t value, size_t alignment, size_t *result)
{
    if (result == NULL || !telos_is_power_of_two(alignment)) {
        return false;
    }
    *result = value & ~(alignment - 1);
    return true;
}

static inline bool
telos_align_up(size_t value, size_t alignment, size_t *result)
{
    size_t adjusted;

    if (result == NULL || !telos_is_power_of_two(alignment) ||
        !telos_size_add(value, alignment - 1, &adjusted)) {
        return false;
    }
    *result = adjusted & ~(alignment - 1);
    return true;
}

#endif
