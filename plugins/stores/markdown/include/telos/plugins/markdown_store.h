#ifndef TELOS_PLUGINS_MARKDOWN_STORE_H
#define TELOS_PLUGINS_MARKDOWN_STORE_H

#include <telos/store.h>

struct telos_event_store *
telos_markdown_store_create(const char *path, struct telos_error **error);

#endif
