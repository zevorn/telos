#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "store_internal.h"

struct ring_store {
    struct telos_event_store base;
    size_t capacity;
    size_t count;
    size_t start;
    uint64_t last_sequence;
    struct telos_event **events;
};

static void ring_destroy(struct telos_event_store *store)
{
    struct ring_store *ring = (struct ring_store *)store;

    for (size_t index = 0; index < ring->count; ++index) {
        const size_t physical = (ring->start + index) % ring->capacity;

        telos_event_release(ring->events[physical]);
    }

    free(ring->events);
    free(ring);
}

static bool ring_append(
    struct telos_event_store *store,
    const struct telos_event *event,
    struct telos_error **error
)
{
    struct ring_store *ring = (struct ring_store *)store;
    const uint64_t sequence = telos_event_sequence(event);
    size_t physical;

    if (ring->last_sequence != 0 && sequence <= ring->last_sequence) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EALREADY,
            "Event sequence is not strictly increasing"
        );
        return false;
    }

    if (ring->count < ring->capacity) {
        physical = (ring->start + ring->count) % ring->capacity;
        ring->count += 1;
    } else {
        physical = ring->start;
        telos_event_release(ring->events[physical]);
        ring->start = (ring->start + 1) % ring->capacity;
    }

    ring->events[physical] = telos_event_retain(event);
    ring->last_sequence = sequence;
    return true;
}

static size_t ring_count(const struct telos_event_store *store)
{
    const struct ring_store *ring = (const struct ring_store *)store;

    return ring->count;
}

static struct telos_event *ring_get(
    const struct telos_event_store *store,
    size_t index,
    struct telos_error **error
)
{
    const struct ring_store *ring = (const struct ring_store *)store;
    size_t physical;

    if (index >= ring->count) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            ERANGE,
            "Ring Store index is out of range"
        );
        return NULL;
    }

    physical = (ring->start + index) % ring->capacity;
    return telos_event_retain(ring->events[physical]);
}

static const struct telos_event_store_ops ring_ops = {
    .destroy = ring_destroy,
    .append = ring_append,
    .count = ring_count,
    .get = ring_get,
};

struct telos_event_store *telos_ring_store_create(
    size_t capacity,
    struct telos_error **error
)
{
    struct ring_store *ring;

    if (error != NULL) {
        *error = NULL;
    }

    if (capacity == 0 || capacity > SIZE_MAX / sizeof(*ring->events)) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Ring Store capacity is invalid"
        );
        return NULL;
    }

    ring = calloc(1, sizeof(*ring));
    if (ring == NULL) {
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Ring Store allocation failed"
        );
        return NULL;
    }

    ring->events = calloc(capacity, sizeof(*ring->events));
    if (ring->events == NULL) {
        free(ring);
        telos_store_set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Ring Store Event allocation failed"
        );
        return NULL;
    }

    ring->base.ops = &ring_ops;
    ring->capacity = capacity;
    return &ring->base;
}
