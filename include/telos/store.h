#ifndef TELOS_STORE_H
#define TELOS_STORE_H

#include <telos/types.h>

#include <telos/error.h>
#include <telos/event.h>

struct telos_event_store;
typedef struct telos_event_store telos_event_store;

typedef telos_event_store *
(*telos_event_store_factory_fn)(const struct telos_value *configuration,
                                struct telos_error **error);

struct telos_event_store_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_event_store_factory_fn create;
};

void telos_event_store_destroy(telos_event_store *store);

bool telos_event_store_append(telos_event_store *store,
                              const struct telos_event *event,
                              struct telos_error **error);

size_t telos_event_store_count(const telos_event_store *store);

struct telos_event *telos_event_store_get(const telos_event_store *store,
                                          size_t index,
                                          struct telos_error **error);

#endif
