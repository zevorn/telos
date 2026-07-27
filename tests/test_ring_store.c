#include <stdio.h>

#include <telos/store.h>

static struct telos_event *new_event(uint64_t sequence)
{
    struct telos_value *payload = telos_value_new_integer((int64_t)sequence);
    const struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = "event",
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
    struct telos_event_store *store = telos_ring_store_create(2, NULL);

    for (uint64_t sequence = 1; sequence <= 3; ++sequence) {
        struct telos_event *event = new_event(sequence);
        const bool appended = telos_event_store_append(store, event, NULL);

        telos_event_release(event);
        if (!appended) {
            fputs("failed to append to Ring Store\n", stderr);
            telos_event_store_destroy(store);
            return 1;
        }
    }

    struct telos_event *first = telos_event_store_get(store, 0, NULL);
    struct telos_event *second = telos_event_store_get(store, 1, NULL);
    if (
        telos_event_store_count(store) != 2
        || telos_event_sequence(first) != 2
        || telos_event_sequence(second) != 3
    ) {
        fputs("Ring Store did not evict only the oldest Event\n", stderr);
        telos_event_release(second);
        telos_event_release(first);
        telos_event_store_destroy(store);
        return 1;
    }

    telos_event_release(second);
    telos_event_release(first);
    telos_event_store_destroy(store);
    return 0;
}
