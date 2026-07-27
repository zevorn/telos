#ifndef TELOS_STORE_INTERNAL_H
#define TELOS_STORE_INTERNAL_H

#include <telos/store.h>

struct telos_event_store_ops {
    void (*destroy)(struct telos_event_store *store);
    bool (*append)(
        struct telos_event_store *store,
        const struct telos_event *event,
        struct telos_error **error
    );
    size_t (*count)(const struct telos_event_store *store);
    struct telos_event *(*get)(
        const struct telos_event_store *store,
        size_t index,
        struct telos_error **error
    );
};

struct telos_event_store {
    const struct telos_event_store_ops *ops;
};

void telos_store_set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
);

#endif
