#include <assert.h>

#include <telos/session.h>

static bool apply(struct telos_session_machine *machine,
                  uint64_t sequence,
                  const char *type)
{
    struct telos_value *payload = telos_value_new_null();
    struct telos_event_spec spec = {
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
    bool result = telos_session_machine_apply(machine, event, NULL);

    telos_event_release(event);
    telos_value_release(payload);
    return result;
}

int main(void)
{
    const struct telos_session_options options = {
        .maximum_retry_attempts = 2,
    };
    struct telos_session_machine *machine =
        telos_session_machine_create_with_options(&options, NULL);

    assert(machine != NULL);
    assert(apply(machine, 1, "turn.accepted"));
    assert(apply(machine, 2, "input.prepare"));
    assert(apply(machine, 3, "context.build"));
    assert(apply(machine, 4, "error.retryable"));
    assert(apply(machine, 5, "retry.resume"));
    assert(apply(machine, 6, "error.retryable"));
    assert(apply(machine, 7, "retry.resume"));
    assert(telos_session_machine_retry_count(machine) == 2);
    assert(apply(machine, 8, "error.retryable"));
    assert(telos_session_machine_state(machine) == TELOS_SESSION_FAILING);
    assert(apply(machine, 9, "failure.completed"));
    assert(telos_session_machine_state(machine) == TELOS_SESSION_FAILED);
    assert(!apply(machine, 10, "failure.completed"));
    telos_session_machine_destroy(machine);
    return 0;
}
