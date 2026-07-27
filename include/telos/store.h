#ifndef TELOS_STORE_H
#define TELOS_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/error.h>
#include <telos/event.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_event_store;

struct telos_event_store *telos_memory_store_create(
    struct telos_error **error
);

struct telos_event_store *telos_ring_store_create(
    size_t capacity,
    struct telos_error **error
);

struct telos_event_store *telos_markdown_store_create(
    const char *path,
    struct telos_error **error
);

void telos_event_store_destroy(struct telos_event_store *store);

bool telos_event_store_append(
    struct telos_event_store *store,
    const struct telos_event *event,
    struct telos_error **error
);

size_t telos_event_store_count(const struct telos_event_store *store);

struct telos_event *telos_event_store_get(
    const struct telos_event_store *store,
    size_t index,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
