#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "store_internal.h"

struct memory_store {
    struct telos_event_store base;
    size_t count;
    size_t capacity;
    struct telos_event **events;
};

static void memory_destroy(struct telos_event_store *store)
{
    struct memory_store *memory = (struct memory_store *)store;

    for (size_t index = 0; index < memory->count; ++index) {
        telos_event_release(memory->events[index]);
    }

    free(memory->events);
    free(memory);
}

static bool memory_append(
    struct telos_event_store *store,
    const struct telos_event *event,
    struct telos_error **error
)
{
    struct memory_store *memory = (struct memory_store *)store;
    struct telos_event **events;
    size_t capacity;

    if (
        memory->count > 0
        && telos_event_sequence(event)
            <= telos_event_sequence(memory->events[memory->count - 1])
    ) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EALREADY,
            "Event sequence is not strictly increasing"
        );
        return false;
    }

    if (memory->count == memory->capacity) {
        capacity = memory->capacity == 0 ? 8 : memory->capacity * 2;
        if (
            capacity < memory->capacity
            || capacity > SIZE_MAX / sizeof(*memory->events)
        ) {
            telos_store_set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Memory Store capacity overflow"
            );
            return false;
        }

        events = realloc(memory->events, capacity * sizeof(*events));
        if (events == NULL) {
            telos_store_set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Memory Store allocation failed"
            );
            return false;
        }

        memory->events = events;
        memory->capacity = capacity;
    }

    memory->events[memory->count] = telos_event_retain(event);
    memory->count += 1;
    return true;
}

static size_t memory_count(const struct telos_event_store *store)
{
    const struct memory_store *memory = (const struct memory_store *)store;

    return memory->count;
}

static struct telos_event *memory_get(
    const struct telos_event_store *store,
    size_t index,
    struct telos_error **error
)
{
    const struct memory_store *memory = (const struct memory_store *)store;

    if (index >= memory->count) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            ERANGE,
            "Memory Store index is out of range"
        );
        return NULL;
    }

    return telos_event_retain(memory->events[index]);
}

static const struct telos_event_store_ops memory_ops = {
    .destroy = memory_destroy,
    .append = memory_append,
    .count = memory_count,
    .get = memory_get,
};

struct telos_event_store *telos_memory_store_create(
    struct telos_error **error
)
{
    struct memory_store *memory;

    if (error != NULL) {
        *error = NULL;
    }

    memory = calloc(1, sizeof(*memory));
    if (memory == NULL) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Memory Store allocation failed"
        );
        return NULL;
    }

    memory->base.ops = &memory_ops;
    return &memory->base;
}
