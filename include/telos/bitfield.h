#ifndef TELOS_BITFIELD_H
#define TELOS_BITFIELD_H

#include <telos/types.h>

static inline unsigned int telos_field_shift_u64(uint64_t mask)
{
    unsigned int shift = 0;

    while (mask != 0 && (mask & UINT64_C(1)) == 0) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

static inline bool telos_field_mask_valid_u64(uint64_t mask)
{
    uint64_t normalized;

    if (mask == 0) {
        return false;
    }
    normalized = mask >> telos_field_shift_u64(mask);
    return (normalized & (normalized + UINT64_C(1))) == 0;
}

static inline uint64_t telos_field_get_u64(uint64_t mask, uint64_t value)
{
    if (!telos_field_mask_valid_u64(mask)) {
        return 0;
    }
    return (value & mask) >> telos_field_shift_u64(mask);
}

static inline bool telos_field_fits_u64(uint64_t mask, uint64_t value)
{
    unsigned int shift;

    if (!telos_field_mask_valid_u64(mask)) {
        return false;
    }
    shift = telos_field_shift_u64(mask);
    return value <= (mask >> shift);
}

static inline bool
telos_field_prepare_u64(uint64_t mask, uint64_t value, uint64_t *result)
{
    if (result == NULL || !telos_field_fits_u64(mask, value)) {
        return false;
    }
    *result = value << telos_field_shift_u64(mask);
    return true;
}

static inline bool telos_field_replace_u64(uint64_t storage,
                                           uint64_t mask,
                                           uint64_t value,
                                           uint64_t *result)
{
    uint64_t prepared;

    if (result == NULL || !telos_field_prepare_u64(mask, value, &prepared)) {
        return false;
    }
    *result = (storage & ~mask) | prepared;
    return true;
}

#define TELOS_FIELD_GET(mask, value)                                           \
    telos_field_get_u64((uint64_t)(mask), (uint64_t)(value))
#define TELOS_FIELD_FITS(mask, value)                                          \
    telos_field_fits_u64((uint64_t)(mask), (uint64_t)(value))
#define TELOS_FIELD_PREP(mask, value, result)                                  \
    telos_field_prepare_u64((uint64_t)(mask), (uint64_t)(value), (result))
#define TELOS_FIELD_REPLACE(storage, mask, value, result)                      \
    telos_field_replace_u64((uint64_t)(storage), (uint64_t)(mask),             \
                            (uint64_t)(value), (result))

#endif
