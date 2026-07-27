#include <stdio.h>

#include <telos/session.h>

static bool apply(
    struct telos_session_machine *machine,
    uint64_t sequence,
    const char *type
)
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
    const bool result = telos_session_machine_apply(machine, event, NULL);

    telos_event_release(event);
    telos_value_release(payload);
    return result;
}

static bool prepare(struct telos_session_machine *machine)
{
    return apply(machine, 1, "turn.accepted")
        && apply(machine, 2, "input.prepare")
        && apply(machine, 3, "context.build");
}

static bool reaches_failed(const char *failure_event)
{
    struct telos_session_machine *machine = telos_session_machine_create(NULL);
    const bool result =
        prepare(machine)
        && apply(machine, 4, failure_event)
        && telos_session_machine_state(machine) == TELOS_SESSION_FAILING
        && apply(machine, 5, "failure.completed")
        && telos_session_machine_state(machine) == TELOS_SESSION_FAILED;

    telos_session_machine_destroy(machine);
    return result;
}

int main(void)
{
    if (!reaches_failed("error.fatal") || !reaches_failed("timeout")) {
        fputs("session did not complete failure path\n", stderr);
        return 1;
    }

    return 0;
}
