#include <errno.h>

#include <telos/store_plugin.h>
/* No-heap path: heap calls below this point fail to compile. */
#define TELOS_NO_HEAP 1
#include <telos/no_heap.h>

void telos_store_set_error(struct telos_error **error,
                           enum telos_error_domain domain, int code,
                           const char *message)
{
    if (error != NULL) {
        *error = telos_error_static(domain, code, message);
    }
}

void telos_event_store_destroy(struct telos_event_store *store)
{
    if (store != NULL) {
        store->ops->destroy(store);
    }
}

bool telos_event_store_append(struct telos_event_store *store,
                              const struct telos_event *event,
                              struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }

    if (store == NULL || event == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                              "Event Store append arguments are invalid");
        return false;
    }

    return store->ops->append(store, event, error);
}

size_t telos_event_store_count(const struct telos_event_store *store)
{
    return store == NULL ? 0 : store->ops->count(store);
}

struct telos_event *telos_event_store_get(const struct telos_event_store *store,
                                          size_t index,
                                          struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }

    if (store == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                              "Event Store is missing");
        return NULL;
    }

    return store->ops->get(store, index, error);
}
