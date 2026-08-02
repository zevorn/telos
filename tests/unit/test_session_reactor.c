#include <assert.h>

#include <telos/actor.h>
#include <telos/plugins/memory_store.h>

static struct telos_event *new_event(uint64_t sequence, const char *type)
{
    struct telos_value *payload = telos_value_new_null();
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
    struct telos_reactor reactor;
    struct telos_event_store *store = telos_memory_store_create(NULL);
    const struct telos_session_actor_spec spec = {
        .store = store,
        .reactor = &reactor,
    };
    struct telos_session_actor *actor;
    struct telos_error *error = NULL;
    const char *steps[] = {
        "turn.accepted",     "input.prepare",     "context.build",
        "provider.dispatch", "response.received", "final.commit",
        "final.committed",
    };

    assert(telos_reactor_initialize(&reactor, NULL, &error));
    assert(error == NULL);
    actor = telos_session_actor_create(&spec, &error);
    assert(actor != NULL && error == NULL);
    for (size_t index = 0; index < sizeof(steps) / sizeof(steps[0]); ++index) {
        struct telos_event *event = new_event(index + 1, steps[index]);

        assert(event != NULL);
        assert(telos_session_actor_submit(actor, event, &error));
        assert(error == NULL);
        telos_event_release(event);
    }
    assert(telos_reactor_pending(&reactor) == 1);
    assert(telos_session_actor_wait_idle(actor, &error));
    assert(error == NULL);
    assert(telos_reactor_pending(&reactor) == 0);
    assert(telos_session_actor_state(actor) == TELOS_SESSION_COMPLETED);
    assert(telos_event_store_count(store) == sizeof(steps) / sizeof(steps[0]));

    telos_session_actor_destroy(actor);
    telos_event_store_destroy(store);
    return 0;
}
