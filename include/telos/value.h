#ifndef TELOS_VALUE_H
#define TELOS_VALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_value_type {
    TELOS_VALUE_NULL = 1,
    TELOS_VALUE_BOOLEAN,
    TELOS_VALUE_INTEGER,
    TELOS_VALUE_REAL,
    TELOS_VALUE_STRING,
    TELOS_VALUE_ARRAY,
    TELOS_VALUE_OBJECT,
    TELOS_VALUE_SENSITIVE,
};

struct telos_value;

struct telos_value *telos_value_new_null(void);

struct telos_value *telos_value_new_boolean(bool value);

struct telos_value *telos_value_new_integer(int64_t value);

struct telos_value *telos_value_new_real(double value);

struct telos_value *telos_value_new_string(const char *value);

struct telos_value *telos_value_new_sensitive(const char *value);

struct telos_value *telos_value_new_array(
    const struct telos_value *const *items,
    size_t count
);

struct telos_value *telos_value_new_object(
    const char *const *keys,
    const struct telos_value *const *values,
    size_t count
);

struct telos_value *telos_value_retain(const struct telos_value *value);

void telos_value_release(const struct telos_value *value);

enum telos_value_type telos_value_type(const struct telos_value *value);

bool telos_value_boolean(const struct telos_value *value, bool *result);

bool telos_value_integer(const struct telos_value *value, int64_t *result);

bool telos_value_real(const struct telos_value *value, double *result);

const char *telos_value_string(const struct telos_value *value);

size_t telos_value_count(const struct telos_value *value);

const struct telos_value *telos_value_at(
    const struct telos_value *value,
    size_t index
);

const char *telos_value_key_at(
    const struct telos_value *value,
    size_t index
);

const struct telos_value *telos_value_get(
    const struct telos_value *value,
    const char *key
);

bool telos_value_equal(
    const struct telos_value *lhs,
    const struct telos_value *rhs
);

size_t telos_value_json_size(const struct telos_value *value);

bool telos_value_write_json(
    const struct telos_value *value,
    char *buffer,
    size_t buffer_size,
    size_t *written,
    struct telos_error **error
);

struct telos_value *telos_value_parse_json(
    const char *json,
    size_t size,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
