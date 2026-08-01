#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/value.h>

struct telos_object_entry {
    char *key;
    struct telos_value *value;
};

struct telos_value {
    atomic_uint references;
    enum telos_value_type type;
    union {
        bool boolean;
        int64_t integer;
        double real;
        char *string;
        struct {
            size_t count;
            struct telos_value **items;
        } array;
        struct {
            size_t count;
            struct telos_object_entry *entries;
        } object;
    } data;
};

static struct telos_value *new_value(enum telos_value_type type)
{
    struct telos_value *result = calloc(1, sizeof(*result));

    if (result == NULL) {
        return NULL;
    }

    atomic_init(&result->references, 1);
    result->type = type;
    return result;
}

struct telos_value *telos_value_new_null(void)
{
    return new_value(TELOS_VALUE_NULL);
}

struct telos_value *telos_value_new_boolean(bool value)
{
    struct telos_value *result = new_value(TELOS_VALUE_BOOLEAN);

    if (result != NULL) {
        result->data.boolean = value;
    }

    return result;
}

struct telos_value *telos_value_new_integer(int64_t value)
{
    struct telos_value *result = new_value(TELOS_VALUE_INTEGER);

    if (result != NULL) {
        result->data.integer = value;
    }

    return result;
}

struct telos_value *telos_value_new_real(double value)
{
    struct telos_value *result = new_value(TELOS_VALUE_REAL);

    if (result != NULL) {
        result->data.real = value;
    }

    return result;
}

struct telos_value *telos_value_new_string(const char *value)
{
    struct telos_value *result;
    size_t size;

    if (value == NULL) {
        return NULL;
    }

    result = new_value(TELOS_VALUE_STRING);
    if (result == NULL) {
        return NULL;
    }

    size = strlen(value) + 1;
    result->data.string = malloc(size);
    if (result->data.string == NULL) {
        free(result);
        return NULL;
    }

    memcpy(result->data.string, value, size);
    return result;
}

struct telos_value *telos_value_new_sensitive(const char *value)
{
    struct telos_value *result;
    size_t size;

    if (value == NULL) {
        return NULL;
    }

    result = new_value(TELOS_VALUE_SENSITIVE);
    if (result == NULL) {
        return NULL;
    }

    size = strlen(value) + 1;
    result->data.string = malloc(size);
    if (result->data.string == NULL) {
        free(result);
        return NULL;
    }

    memcpy(result->data.string, value, size);
    return result;
}

struct telos_value *
telos_value_new_array(const struct telos_value *const *items, size_t count)
{
    struct telos_value *result;

    if (count > 0 && items == NULL) {
        return NULL;
    }

    if (count > SIZE_MAX / sizeof(*result->data.array.items)) {
        return NULL;
    }

    result = new_value(TELOS_VALUE_ARRAY);
    if (result == NULL) {
        return NULL;
    }

    if (count > 0) {
        result->data.array.items =
            calloc(count, sizeof(*result->data.array.items));
        if (result->data.array.items == NULL) {
            free(result);
            return NULL;
        }
    }

    result->data.array.count = count;
    for (size_t index = 0; index < count; ++index) {
        if (items[index] == NULL) {
            telos_value_release(result);
            return NULL;
        }

        result->data.array.items[index] = telos_value_retain(items[index]);
    }

    return result;
}

struct telos_value *
telos_value_new_object(const char *const *keys,
                       const struct telos_value *const *values,
                       size_t count)
{
    struct telos_value *result;

    if (count > 0 && (keys == NULL || values == NULL)) {
        return NULL;
    }

    if (count > SIZE_MAX / sizeof(*result->data.object.entries)) {
        return NULL;
    }

    result = new_value(TELOS_VALUE_OBJECT);
    if (result == NULL) {
        return NULL;
    }

    if (count > 0) {
        result->data.object.entries =
            calloc(count, sizeof(*result->data.object.entries));
        if (result->data.object.entries == NULL) {
            free(result);
            return NULL;
        }
    }

    for (size_t index = 0; index < count; ++index) {
        size_t key_size;

        if (keys[index] == NULL || values[index] == NULL) {
            telos_value_release(result);
            return NULL;
        }

        for (size_t prior = 0; prior < index; ++prior) {
            if (strcmp(keys[prior], keys[index]) == 0) {
                telos_value_release(result);
                return NULL;
            }
        }

        key_size = strlen(keys[index]) + 1;
        result->data.object.entries[index].key = malloc(key_size);
        if (result->data.object.entries[index].key == NULL) {
            telos_value_release(result);
            return NULL;
        }

        memcpy(result->data.object.entries[index].key, keys[index], key_size);
        result->data.object.entries[index].value =
            telos_value_retain(values[index]);
        result->data.object.count += 1;
    }

    return result;
}

struct telos_value *telos_value_retain(const struct telos_value *value)
{
    struct telos_value *mutable_value = (struct telos_value *)value;

    if (mutable_value != NULL) {
        atomic_fetch_add_explicit(&mutable_value->references, 1,
                                  memory_order_relaxed);
    }

    return mutable_value;
}

void telos_value_release(const struct telos_value *value)
{
    struct telos_value *mutable_value = (struct telos_value *)value;

    if (mutable_value == NULL) {
        return;
    }

    if (atomic_fetch_sub_explicit(&mutable_value->references, 1,
                                  memory_order_acq_rel) == 1) {
        if (mutable_value->type == TELOS_VALUE_STRING ||
            mutable_value->type == TELOS_VALUE_SENSITIVE) {
            free(mutable_value->data.string);
        } else if (mutable_value->type == TELOS_VALUE_ARRAY) {
            for (size_t index = 0; index < mutable_value->data.array.count;
                 ++index) {
                telos_value_release(mutable_value->data.array.items[index]);
            }
            free(mutable_value->data.array.items);
        } else if (mutable_value->type == TELOS_VALUE_OBJECT) {
            for (size_t index = 0; index < mutable_value->data.object.count;
                 ++index) {
                free(mutable_value->data.object.entries[index].key);
                telos_value_release(
                    mutable_value->data.object.entries[index].value);
            }
            free(mutable_value->data.object.entries);
        }
        free(mutable_value);
    }
}

enum telos_value_type telos_value_type(const struct telos_value *value)
{
    if (value == NULL) {
        return 0;
    }

    return value->type;
}

bool telos_value_boolean(const struct telos_value *value, bool *result)
{
    if (value == NULL || result == NULL || value->type != TELOS_VALUE_BOOLEAN) {
        return false;
    }

    *result = value->data.boolean;
    return true;
}

bool telos_value_integer(const struct telos_value *value, int64_t *result)
{
    if (value == NULL || result == NULL || value->type != TELOS_VALUE_INTEGER) {
        return false;
    }

    *result = value->data.integer;
    return true;
}

bool telos_value_real(const struct telos_value *value, double *result)
{
    if (value == NULL || result == NULL || value->type != TELOS_VALUE_REAL) {
        return false;
    }

    *result = value->data.real;
    return true;
}

const char *telos_value_string(const struct telos_value *value)
{
    if (value == NULL || value->type != TELOS_VALUE_STRING) {
        return NULL;
    }

    return value->data.string;
}

size_t telos_value_count(const struct telos_value *value)
{
    if (value == NULL) {
        return 0;
    }

    if (value->type == TELOS_VALUE_ARRAY) {
        return value->data.array.count;
    }

    if (value->type == TELOS_VALUE_OBJECT) {
        return value->data.object.count;
    }

    return 0;
}

const struct telos_value *telos_value_at(const struct telos_value *value,
                                         size_t index)
{
    if (value == NULL || value->type != TELOS_VALUE_ARRAY ||
        index >= value->data.array.count) {
        return NULL;
    }

    return value->data.array.items[index];
}

const char *telos_value_key_at(const struct telos_value *value, size_t index)
{
    if (value == NULL || value->type != TELOS_VALUE_OBJECT ||
        index >= value->data.object.count) {
        return NULL;
    }

    return value->data.object.entries[index].key;
}

const struct telos_value *telos_value_get(const struct telos_value *value,
                                          const char *key)
{
    if (value == NULL || value->type != TELOS_VALUE_OBJECT || key == NULL) {
        return NULL;
    }

    for (size_t index = 0; index < value->data.object.count; ++index) {
        if (strcmp(value->data.object.entries[index].key, key) == 0) {
            return value->data.object.entries[index].value;
        }
    }

    return NULL;
}

bool telos_value_equal(const struct telos_value *lhs,
                       const struct telos_value *rhs)
{
    if (lhs == rhs) {
        return true;
    }
    if (lhs == NULL || rhs == NULL || lhs->type != rhs->type) {
        return false;
    }

    switch (lhs->type) {
    case TELOS_VALUE_NULL:
        return true;
    case TELOS_VALUE_BOOLEAN:
        return lhs->data.boolean == rhs->data.boolean;
    case TELOS_VALUE_INTEGER:
        return lhs->data.integer == rhs->data.integer;
    case TELOS_VALUE_REAL:
        return lhs->data.real == rhs->data.real;
    case TELOS_VALUE_STRING:
    case TELOS_VALUE_SENSITIVE:
        return strcmp(lhs->data.string, rhs->data.string) == 0;
    case TELOS_VALUE_ARRAY:
        if (lhs->data.array.count != rhs->data.array.count) {
            return false;
        }
        for (size_t index = 0; index < lhs->data.array.count; ++index) {
            if (!telos_value_equal(lhs->data.array.items[index],
                                   rhs->data.array.items[index])) {
                return false;
            }
        }
        return true;
    case TELOS_VALUE_OBJECT:
        if (lhs->data.object.count != rhs->data.object.count) {
            return false;
        }
        for (size_t index = 0; index < lhs->data.object.count; ++index) {
            const struct telos_object_entry *entry =
                &lhs->data.object.entries[index];
            const struct telos_value *other = telos_value_get(rhs, entry->key);

            if (!telos_value_equal(entry->value, other)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}
