#ifndef TELOS_PLUGINS_JSONL_STORE_H
#define TELOS_PLUGINS_JSONL_STORE_H

#include <telos/store.h>

struct telos_event_store *
telos_jsonl_store_create(const char *path, struct telos_error **error);

#endif
