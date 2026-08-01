#include <stdint.h>
#include <stdio.h>

#include <telos/plugins/markdown_store.h>
#include <telos/plugins/memory_store.h>
#include <telos/plugins/ring_store.h>

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

static bool test_store(struct telos_event_store *store)
{
    struct telos_error *error = NULL;
    bool passed = store != NULL;

    for (uint64_t sequence = 1; passed && sequence <= 10; ++sequence) {
        struct telos_event *event = new_event(sequence);

        passed = event != NULL && telos_event_store_append(store, event, NULL);
        telos_event_release(event);
    }
    {
        struct telos_event *duplicate = new_event(10);
        struct telos_event *older = new_event(9);

        passed = passed &&
                 !telos_event_store_append(store, duplicate, &error) &&
                 error != NULL;
        telos_error_release(error);
        error = NULL;
        passed = passed && !telos_event_store_append(store, older, &error) &&
                 error != NULL;
        telos_error_release(error);
        error = NULL;
        telos_event_release(older);
        telos_event_release(duplicate);
    }
    passed = passed &&
             telos_event_store_get(store, telos_event_store_count(store),
                                   &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    telos_event_store_destroy(store);
    return passed;
}

int main(void)
{
    struct telos_error *error = NULL;
    struct telos_event_store *invalid;
    bool passed = telos_event_store_count(NULL) == 0 &&
                  !telos_event_store_append(NULL, NULL, &error) &&
                  error != NULL;

    telos_error_release(error);
    error = NULL;
    passed = passed && telos_event_store_get(NULL, 0, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    invalid = telos_ring_store_create(0, &error);
    passed = passed && invalid == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    invalid = telos_ring_store_create(SIZE_MAX, &error);
    passed = passed && invalid == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    invalid = telos_markdown_store_create(NULL, &error);
    passed = passed && invalid == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    invalid = telos_markdown_store_create("", &error);
    passed = passed && invalid == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    invalid =
        telos_markdown_store_create("/missing/telos/store/events.md", &error);
    passed = passed && invalid == NULL && error != NULL;
    telos_error_release(error);

    telos_event_store_destroy(NULL);
    passed = passed && test_store(telos_memory_store_create(NULL)) &&
             test_store(telos_ring_store_create(16, NULL));
    if (!passed) {
        fputs("Event Store validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
