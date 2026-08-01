#ifndef TELOS_STATIC_BUFFER_H
#define TELOS_STATIC_BUFFER_H

#include <string.h>

#include <telos/checked_math.h>
#include <telos/types.h>

struct telos_static_buffer {
    char *data;
    size_t capacity;
    size_t size;
};

static inline bool
telos_static_buffer_initialize(struct telos_static_buffer *buffer,
                               char *storage,
                               size_t capacity)
{
    if (buffer == NULL || storage == NULL || capacity == 0) {
        return false;
    }
    buffer->data = storage;
    buffer->capacity = capacity;
    buffer->size = 0;
    storage[0] = '\0';
    return true;
}

static inline bool
telos_static_buffer_append_n(struct telos_static_buffer *buffer,
                             const char *text,
                             size_t size)
{
    size_t required;

    if (buffer == NULL || text == NULL ||
        !telos_size_add(buffer->size, size, &required) ||
        !telos_size_add(required, 1, &required) ||
        required > buffer->capacity) {
        return false;
    }
    memcpy(buffer->data + buffer->size, text, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return true;
}

static inline bool
telos_static_buffer_append(struct telos_static_buffer *buffer, const char *text)
{
    return text != NULL &&
           telos_static_buffer_append_n(buffer, text, strlen(text));
}

static inline size_t
telos_static_buffer_remaining(const struct telos_static_buffer *buffer)
{
    if (buffer == NULL || buffer->capacity <= buffer->size) {
        return 0;
    }
    return buffer->capacity - buffer->size - 1;
}

#endif
