#ifndef TELOS_PLUGINS_MEMORY_STORE_H
#define TELOS_PLUGINS_MEMORY_STORE_H

#include <telos/store.h>

struct telos_event_store *telos_memory_store_create(struct telos_error **error);

#endif
