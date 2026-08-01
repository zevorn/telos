#include <stdio.h>

#include <telos/session.h>

static bool apply(struct telos_session_machine *machine,
                  uint64_t sequence,
                  const char *type)
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

int main(void)
{
    struct telos_session_machine *machine = telos_session_machine_create(NULL);
    const char *setup[] = {
        "turn.accepted",
        "input.prepare",
        "context.build",
        "provider.dispatch",
    };

    for (size_t index = 0; index < sizeof(setup) / sizeof(setup[0]); ++index) {
        if (!apply(machine, index + 1, setup[index])) {
            fputs("failed to reach provider dispatch\n", stderr);
            telos_session_machine_destroy(machine);
            return 1;
        }
    }

    if (!apply(machine, 5, "cancel.requested") ||
        telos_session_machine_state(machine) != TELOS_SESSION_CANCELLING ||
        !apply(machine, 6, "cancel.completed") ||
        telos_session_machine_state(machine) != TELOS_SESSION_CANCELLED) {
        fputs("session did not complete cancellation path\n", stderr);
        telos_session_machine_destroy(machine);
        return 1;
    }

    telos_session_machine_destroy(machine);
    return 0;
}
