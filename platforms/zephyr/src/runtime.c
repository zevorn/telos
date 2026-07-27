#include <zephyr/kernel.h>

#include <telos/event.h>
#include <telos/session.h>
#include <telos/value.h>
#include <telos/zephyr.h>

static bool initialized;
static const char *last_trace = "trace: no events";

int telos_zephyr_initialize(void)
{
    initialized = true;
    return 0;
}

const char *telos_zephyr_status(void)
{
    return initialized ? "ready" : "not-initialized";
}

static bool apply_event(
    struct telos_session_machine *machine,
    uint64_t sequence,
    const char *type,
    const struct telos_value *payload
)
{
    struct telos_id id = telos_id_generate();
    struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = id,
        .session_id = {.high = 1, .low = 1},
        .correlation_id = {.high = 1, .low = 2},
        .causation_id = {.high = 1, .low = sequence - 1},
        .type = type,
        .source = "static:dev.zevorn.echo",
        .timestamp_milliseconds = (int64_t)sequence,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);
    bool result = event != NULL
        && telos_session_machine_apply(machine, event, NULL);

    telos_event_release(event);
    return result;
}

bool telos_zephyr_run_static_scenario(void)
{
    static const char *events[] = {
        "turn.accepted",
        "input.prepare",
        "context.build",
        "provider.dispatch",
        "response.received",
        "tool.authorize",
    };
    struct telos_session_machine *machine;
    struct telos_value *empty;
    struct telos_value *tool_count;
    struct telos_value *echo_input;
    struct telos_value *echo_output;
    uint64_t sequence = 1;
    bool result = initialized;

    machine = telos_session_machine_create(NULL);
    empty = telos_value_new_null();
    tool_count = telos_value_new_integer(1);
    echo_input = telos_value_new_string("zephyr");
    echo_output = telos_value_retain(echo_input);
    result = result
        && machine != NULL
        && empty != NULL
        && tool_count != NULL
        && echo_output != NULL;
    for (
        size_t index = 0;
        result && index < sizeof(events) / sizeof(events[0]);
        ++index
    ) {
        result = apply_event(machine, sequence++, events[index], empty);
    }
    result = result
        && apply_event(machine, sequence++, "tool.execute", tool_count)
        && apply_event(machine, sequence++, "tool.completed", echo_output)
        && apply_event(machine, sequence++, "context.build", empty)
        && apply_event(machine, sequence++, "provider.dispatch", empty)
        && apply_event(machine, sequence++, "response.received", empty)
        && apply_event(machine, sequence++, "final.commit", empty)
        && apply_event(machine, sequence++, "final.committed", empty)
        && telos_session_machine_state(machine) == TELOS_SESSION_COMPLETED;
    if (result) {
        last_trace = "trace: static echo output=zephyr state=COMPLETED";
    }
    telos_value_release(echo_output);
    telos_value_release(echo_input);
    telos_value_release(tool_count);
    telos_value_release(empty);
    telos_session_machine_destroy(machine);
    return result;
}

const char *telos_zephyr_trace(void)
{
    return last_trace;
}
