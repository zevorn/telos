#include <stdio.h>
#include <string.h>

#include <telos/plugins/memory_store.h>

static struct telos_event *new_event(uint64_t sequence, const char *type)
{
    struct telos_value *payload = telos_value_new_string(type);
    const struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = type,
        .source = "test",
        .timestamp_milliseconds = (int64_t)sequence,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);

    telos_value_release(payload);
    return event;
}

int main(void)
{
    struct telos_event_store *store = telos_memory_store_create(NULL);
    struct telos_event *first = new_event(1, "first");
    struct telos_event *second = new_event(2, "second");
    struct telos_event *stored_first;
    struct telos_event *stored_second;

    if (store == NULL || !telos_event_store_append(store, first, NULL) ||
        !telos_event_store_append(store, second, NULL)) {
        fputs("failed to append ordered Events\n", stderr);
        telos_event_release(second);
        telos_event_release(first);
        telos_event_store_destroy(store);
        return 1;
    }

    telos_event_release(second);
    telos_event_release(first);
    stored_first = telos_event_store_get(store, 0, NULL);
    stored_second = telos_event_store_get(store, 1, NULL);

    if (telos_event_store_count(store) != 2 || stored_first == NULL ||
        stored_second == NULL ||
        strcmp(telos_event_type(stored_first), "first") != 0 ||
        strcmp(telos_event_type(stored_second), "second") != 0) {
        fputs("Memory Store changed Event order or ownership\n", stderr);
        telos_event_release(stored_second);
        telos_event_release(stored_first);
        telos_event_store_destroy(store);
        return 1;
    }

    telos_event_release(stored_second);
    telos_event_release(stored_first);
    telos_event_store_destroy(store);
    return 0;
}
