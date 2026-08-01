#include <stdio.h>

#include <telos/session.h>

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
    static const struct {
        const char *event_type;
        enum telos_session_state expected_state;
    } steps[] = {
        {"turn.accepted", TELOS_SESSION_TURN_ACCEPTED},
        {"input.prepare", TELOS_SESSION_INPUT_PREPARE},
        {"context.build", TELOS_SESSION_CONTEXT_BUILD},
        {"provider.dispatch", TELOS_SESSION_PROVIDER_DISPATCH},
        {"response.received", TELOS_SESSION_RESPONSE_PROCESS},
        {"final.commit", TELOS_SESSION_FINAL_COMMIT},
        {"final.committed", TELOS_SESSION_COMPLETED},
    };
    struct telos_error *error = NULL;
    struct telos_session_machine *machine =
        telos_session_machine_create(&error);

    if (machine == NULL || error != NULL ||
        telos_session_machine_state(machine) != TELOS_SESSION_IDLE) {
        fputs("session machine did not start idle\n", stderr);
        telos_error_release(error);
        telos_session_machine_destroy(machine);
        return 1;
    }

    for (size_t index = 0; index < sizeof(steps) / sizeof(steps[0]); ++index) {
        struct telos_event *event =
            new_event(index + 1, steps[index].event_type);
        const bool applied =
            telos_session_machine_apply(machine, event, &error);

        telos_event_release(event);
        if (!applied || error != NULL ||
            telos_session_machine_state(machine) !=
                steps[index].expected_state) {
            fputs("session did not follow no-tool turn\n", stderr);
            telos_error_release(error);
            telos_session_machine_destroy(machine);
            return 1;
        }
    }

    telos_session_machine_destroy(machine);
    return 0;
}
