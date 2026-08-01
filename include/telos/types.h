#ifndef TELOS_TYPES_H
#define TELOS_TYPES_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(CHAR_BIT == 8, "Telos requires 8-bit bytes");

typedef int8_t telos_i8;
typedef int16_t telos_i16;
typedef int32_t telos_i32;
typedef int64_t telos_i64;

typedef uint8_t telos_u8;
typedef uint16_t telos_u16;
typedef uint32_t telos_u32;
typedef uint64_t telos_u64;

typedef size_t telos_size;
typedef ptrdiff_t telos_ssize;
typedef intptr_t telos_iptr;
typedef uintptr_t telos_uptr;
typedef int64_t telos_offset;
typedef max_align_t telos_max_align;
typedef unsigned char telos_byte;

#define TELOS_SIZE_MAX SIZE_MAX
#define TELOS_SSIZE_MIN PTRDIFF_MIN
#define TELOS_SSIZE_MAX PTRDIFF_MAX
#define TELOS_OFFSET_MIN INT64_MIN
#define TELOS_OFFSET_MAX INT64_MAX

#endif
