#ifndef TELOS_CHECKED_MATH_H
#define TELOS_CHECKED_MATH_H

#include <telos/types.h>

static inline bool telos_size_add(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

static inline bool
telos_size_multiply(size_t left, size_t right, size_t *result)
{
    if (result == NULL || (left != 0 && right > SIZE_MAX / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

#endif
