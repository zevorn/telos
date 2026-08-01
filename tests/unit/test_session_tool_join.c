#include <stdio.h>

#include <telos/session.h>

static bool apply(struct telos_session_machine *machine,
                  uint64_t sequence,
                  const char *type,
                  int64_t tool_count)
{
    struct telos_value *payload = tool_count < 0
                                      ? telos_value_new_null()
                                      : telos_value_new_integer(tool_count);
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
        "turn.accepted",     "input.prepare",     "context.build",
        "provider.dispatch", "response.received", "tool.authorize",
    };
    uint64_t sequence = 1;

    for (size_t index = 0; index < sizeof(setup) / sizeof(setup[0]); ++index) {
        if (!apply(machine, sequence++, setup[index], -1)) {
            return 1;
        }
    }

    if (!apply(machine, sequence++, "tool.execute", 3) ||
        !apply(machine, sequence++, "tool.completed", -1) ||
        telos_session_machine_state(machine) != TELOS_SESSION_TOOL_EXECUTE ||
        apply(machine, sequence, "context.build", -1) ||
        !apply(machine, sequence++, "tool.completed", -1) ||
        telos_session_machine_state(machine) != TELOS_SESSION_TOOL_EXECUTE ||
        !apply(machine, sequence++, "tool.completed", -1) ||
        telos_session_machine_state(machine) != TELOS_SESSION_TOOL_COLLECT ||
        !apply(machine, sequence, "context.build", -1) ||
        telos_session_machine_state(machine) != TELOS_SESSION_CONTEXT_BUILD) {
        fputs("Tool Calls did not join before Provider continuation\n", stderr);
        telos_session_machine_destroy(machine);
        return 1;
    }

    telos_session_machine_destroy(machine);
    return 0;
}
