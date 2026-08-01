#ifndef TELOS_PLUGINS_RING_STORE_H
#define TELOS_PLUGINS_RING_STORE_H

#include <telos/store.h>

#define TELOS_RING_STORE_STORAGE_SIZE 64U

union telos_ring_store_storage {
    telos_max_align alignment;
    telos_byte bytes[TELOS_RING_STORE_STORAGE_SIZE];
};

bool telos_ring_store_initialize(union telos_ring_store_storage *storage,
                                 struct telos_event **event_slots,
                                 telos_size capacity,
                                 struct telos_event_store **store,
                                 struct telos_error **error);

struct telos_event_store *telos_ring_store_create(telos_size capacity,
                                                  struct telos_error **error);

#endif
