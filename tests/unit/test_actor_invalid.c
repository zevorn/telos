#include <stdio.h>

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

static bool transition_failure(void)
{
    struct telos_session_actor *actor = telos_session_actor_create(NULL, NULL);
    struct telos_event *first = new_event(1, "turn.accepted");
    struct telos_event *invalid = new_event(2, "turn.accepted");
    struct telos_event *later = new_event(3, "input.prepare");
    struct telos_error *error = NULL;
    bool passed = actor != NULL &&
                  telos_session_actor_state(actor) == TELOS_SESSION_IDLE &&
                  telos_session_actor_wait_idle(actor, NULL) &&
                  telos_session_actor_submit(actor, first, NULL) &&
                  telos_session_actor_submit(actor, invalid, NULL) &&
                  !telos_session_actor_wait_idle(actor, &error) &&
                  error != NULL;

    telos_error_release(error);
    error = NULL;
    passed = passed &&
             telos_session_actor_state(actor) == TELOS_SESSION_TURN_ACCEPTED &&
             !telos_session_actor_submit(actor, later, &error) &&
             error != NULL && telos_error_cause(error) != NULL;
    telos_error_release(error);
    telos_event_release(later);
    telos_event_release(invalid);
    telos_event_release(first);
    telos_session_actor_destroy(actor);
    return passed;
}

static bool store_failure(void)
{
    struct telos_event_store *store = telos_memory_store_create(NULL);
    struct telos_event *stored = new_event(10, "existing");
    const struct telos_session_actor_spec spec = {.store = store};
    struct telos_session_actor *actor;
    struct telos_event *event = new_event(1, "turn.accepted");
    struct telos_error *error = NULL;
    bool passed = telos_event_store_append(store, stored, NULL);

    actor = telos_session_actor_create(&spec, NULL);
    passed = passed && actor != NULL &&
             telos_session_actor_submit(actor, event, NULL) &&
             !telos_session_actor_wait_idle(actor, &error) && error != NULL &&
             telos_error_cause(error) != NULL &&
             telos_event_store_count(store) == 1;
    telos_error_release(error);
    telos_event_release(event);
    telos_event_release(stored);
    telos_session_actor_destroy(actor);
    telos_event_store_destroy(store);
    return passed;
}

int main(void)
{
    struct telos_error *error = NULL;
    struct telos_event *event = new_event(1, "turn.accepted");
    struct telos_session_actor *actor = NULL;
    bool passed = telos_session_actor_state(NULL) == 0 &&
                  !telos_session_actor_submit(NULL, event, &error) &&
                  error != NULL;

    telos_error_release(error);
    error = NULL;
    passed =
        passed && !telos_session_actor_wait_idle(NULL, &error) && error != NULL;
    telos_error_release(error);
    error = NULL;
    actor = telos_session_actor_create(NULL, NULL);
    passed = passed && actor != NULL &&
             !telos_session_actor_submit(actor, NULL, &error) && error != NULL;
    telos_error_release(error);
    telos_session_actor_destroy(actor);
    telos_event_release(event);
    telos_session_actor_destroy(NULL);

    if (!passed || !transition_failure() || !store_failure()) {
        fputs("Session Actor failure matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
