#ifndef TELOS_BITOPS_H
#define TELOS_BITOPS_H

#include <telos/types.h>

#define TELOS_BITMAP_WORD_BITS 32U
#define TELOS_BITMAP_WORDS(bit_count)                                          \
    ((size_t)(bit_count) / TELOS_BITMAP_WORD_BITS +                            \
     ((size_t)(bit_count) % TELOS_BITMAP_WORD_BITS != 0U))
#define TELOS_DECLARE_BITMAP(name, bit_count)                                  \
    uint32_t name[TELOS_BITMAP_WORDS(bit_count)]
#define TELOS_BIT_U32(bit) (UINT32_C(1) << (bit))
#define TELOS_BIT_U64(bit) (UINT64_C(1) << (bit))

static inline size_t telos_bitmap_word(size_t bit)
{
    return bit / TELOS_BITMAP_WORD_BITS;
}

static inline uint32_t telos_bitmap_mask(size_t bit)
{
    return TELOS_BIT_U32(bit % TELOS_BITMAP_WORD_BITS);
}

static inline bool
telos_bitmap_test(const uint32_t *bitmap, size_t bit_count, size_t bit)
{
    return bitmap != NULL && bit < bit_count &&
           (bitmap[telos_bitmap_word(bit)] & telos_bitmap_mask(bit)) != 0;
}

static inline bool
telos_bitmap_set(uint32_t *bitmap, size_t bit_count, size_t bit)
{
    if (bitmap == NULL || bit >= bit_count) {
        return false;
    }
    bitmap[telos_bitmap_word(bit)] |= telos_bitmap_mask(bit);
    return true;
}

static inline bool
telos_bitmap_clear(uint32_t *bitmap, size_t bit_count, size_t bit)
{
    if (bitmap == NULL || bit >= bit_count) {
        return false;
    }
    bitmap[telos_bitmap_word(bit)] &= ~telos_bitmap_mask(bit);
    return true;
}

static inline bool
telos_bitmap_toggle(uint32_t *bitmap, size_t bit_count, size_t bit)
{
    if (bitmap == NULL || bit >= bit_count) {
        return false;
    }
    bitmap[telos_bitmap_word(bit)] ^= telos_bitmap_mask(bit);
    return true;
}

static inline bool telos_bitmap_test_and_set(uint32_t *bitmap,
                                             size_t bit_count,
                                             size_t bit,
                                             bool *previous)
{
    if (previous == NULL || bitmap == NULL || bit >= bit_count) {
        return false;
    }
    *previous = telos_bitmap_test(bitmap, bit_count, bit);
    bitmap[telos_bitmap_word(bit)] |= telos_bitmap_mask(bit);
    return true;
}

static inline bool telos_bitmap_test_and_clear(uint32_t *bitmap,
                                               size_t bit_count,
                                               size_t bit,
                                               bool *previous)
{
    if (previous == NULL || bitmap == NULL || bit >= bit_count) {
        return false;
    }
    *previous = telos_bitmap_test(bitmap, bit_count, bit);
    bitmap[telos_bitmap_word(bit)] &= ~telos_bitmap_mask(bit);
    return true;
}

#endif
