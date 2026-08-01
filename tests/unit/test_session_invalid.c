#include <assert.h>
#include <errno.h>

#include <telos/session.h>

static struct telos_event *new_event(uint64_t sequence,
                                     const char *type,
                                     const struct telos_value *payload)
{
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

    return telos_event_create(&spec, NULL);
}

static bool apply(struct telos_session_machine *machine,
                  uint64_t sequence,
                  const char *type,
                  const struct telos_value *payload,
                  struct telos_error **error)
{
    struct telos_event *event = new_event(sequence, type, payload);
    bool result = telos_session_machine_apply(machine, event, error);

    telos_event_release(event);
    return result;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

static uint64_t drive_to_response(struct telos_session_machine *machine,
                                  const struct telos_value *payload)
{
    const char *steps[] = {
        "turn.accepted",     "input.prepare",     "context.build",
        "provider.dispatch", "response.received",
    };

    for (size_t index = 0; index < 5; ++index) {
        assert(apply(machine, index + 1, steps[index], payload, NULL));
    }
    return 5;
}

static struct telos_session_machine *
completed_machine(const struct telos_value *payload)
{
    struct telos_session_machine *machine = telos_session_machine_create(NULL);
    uint64_t sequence = drive_to_response(machine, payload);

    assert(apply(machine, ++sequence, "final.commit", payload, NULL));
    assert(apply(machine, ++sequence, "final.committed", payload, NULL));
    return machine;
}

static struct telos_session_machine *
cancelled_machine(const struct telos_value *payload)
{
    struct telos_session_machine *machine = telos_session_machine_create(NULL);

    assert(apply(machine, 1, "turn.accepted", payload, NULL));
    assert(apply(machine, 2, "cancel.requested", payload, NULL));
    assert(apply(machine, 3, "cancel.completed", payload, NULL));
    return machine;
}

static struct telos_session_machine *
failed_machine(const struct telos_value *payload)
{
    struct telos_session_machine *machine = telos_session_machine_create(NULL);

    assert(apply(machine, 1, "turn.accepted", payload, NULL));
    assert(apply(machine, 2, "error.fatal", payload, NULL));
    assert(apply(machine, 3, "failure.completed", payload, NULL));
    return machine;
}

static void assert_rejected(struct telos_session_machine *machine,
                            uint64_t sequence,
                            const char *type,
                            const struct telos_value *payload)
{
    struct telos_error *error = NULL;

    assert(!apply(machine, sequence, type, payload, &error));
    assert(error != NULL);
    clear_error(&error);
}

int main(void)
{
    struct telos_error *error = NULL;
    const struct telos_session_options zero = {
        .maximum_retry_attempts = 0,
    };
    struct telos_value *null_value = telos_value_new_null();
    struct telos_value *two = telos_value_new_integer(2);
    struct telos_value *zero_value = telos_value_new_integer(0);
    struct telos_event *event = new_event(1, "turn.accepted", null_value);
    struct telos_session_machine *machine;

    assert(telos_session_machine_create_with_options(NULL, &error) == NULL);
    assert(telos_error_code(error) == EINVAL);
    clear_error(&error);
    assert(telos_session_machine_create_with_options(&zero, &error) == NULL);
    assert(telos_error_code(error) == EINVAL);
    clear_error(&error);
    assert(telos_session_machine_state(NULL) == 0);
    assert(telos_session_machine_retry_count(NULL) == 0);
    assert(!telos_session_machine_apply(NULL, event, &error));
    clear_error(&error);
    machine = telos_session_machine_create(NULL);
    assert(!telos_session_machine_apply(machine, NULL, &error));
    clear_error(&error);
    assert(telos_session_machine_apply(machine, event, &error));
    assert(!telos_session_machine_apply(machine, event, &error));
    assert(telos_error_code(error) == EALREADY);
    clear_error(&error);
    assert_rejected(machine, 2, "not.valid", null_value);
    telos_session_machine_destroy(machine);
    telos_event_release(event);

    machine = telos_session_machine_create(NULL);
    drive_to_response(machine, null_value);
    assert(apply(machine, 6, "tool.authorize", null_value, NULL));
    assert(apply(machine, 7, "tool.execute", two, NULL));
    assert_rejected(machine, 8, "tool.collect", null_value);
    assert(apply(machine, 8, "tool.completed", null_value, NULL));
    assert(telos_session_machine_state(machine) == TELOS_SESSION_TOOL_EXECUTE);
    assert(apply(machine, 9, "tool.completed", null_value, NULL));
    assert(telos_session_machine_state(machine) == TELOS_SESSION_TOOL_COLLECT);
    assert_rejected(machine, 10, "tool.completed", null_value);
    telos_session_machine_destroy(machine);

    machine = telos_session_machine_create(NULL);
    drive_to_response(machine, null_value);
    assert(apply(machine, 6, "tool.authorize", null_value, NULL));
    assert(apply(machine, 7, "tool.execute", zero_value, NULL));
    assert_rejected(machine, 8, "tool.completed", null_value);
    telos_session_machine_destroy(machine);

    machine = telos_session_machine_create(NULL);
    assert_rejected(machine, 1, "cancel.requested", null_value);
    assert_rejected(machine, 2, "error.retryable", null_value);
    assert_rejected(machine, 3, "error.fatal", null_value);
    assert_rejected(machine, 4, "timeout", null_value);
    assert_rejected(machine, 5, "retry.resume", null_value);
    telos_session_machine_destroy(machine);

    machine = completed_machine(null_value);
    assert_rejected(machine, 8, "cancel.requested", null_value);
    assert_rejected(machine, 9, "error.retryable", null_value);
    assert_rejected(machine, 10, "error.fatal", null_value);
    telos_session_machine_destroy(machine);

    machine = cancelled_machine(null_value);
    assert_rejected(machine, 4, "cancel.requested", null_value);
    assert_rejected(machine, 5, "error.retryable", null_value);
    assert_rejected(machine, 6, "timeout", null_value);
    telos_session_machine_destroy(machine);

    machine = failed_machine(null_value);
    assert_rejected(machine, 4, "cancel.requested", null_value);
    assert_rejected(machine, 5, "error.retryable", null_value);
    assert_rejected(machine, 6, "error.fatal", null_value);
    telos_session_machine_destroy(machine);

    machine = telos_session_machine_create(NULL);
    assert(apply(machine, 1, "turn.accepted", null_value, NULL));
    assert(apply(machine, 2, "cancel.requested", null_value, NULL));
    assert_rejected(machine, 3, "cancel.requested", null_value);
    telos_session_machine_destroy(machine);

    machine = telos_session_machine_create(NULL);
    assert(apply(machine, 1, "turn.accepted", null_value, NULL));
    assert(apply(machine, 2, "error.retryable", null_value, NULL));
    assert_rejected(machine, 3, "error.retryable", null_value);
    assert(apply(machine, 3, "retry.resume", null_value, NULL));
    telos_session_machine_destroy(machine);

    machine = telos_session_machine_create(NULL);
    assert(apply(machine, 1, "turn.accepted", null_value, NULL));
    assert(apply(machine, 2, "error.fatal", null_value, NULL));
    assert_rejected(machine, 3, "error.fatal", null_value);
    telos_session_machine_destroy(machine);

    telos_session_machine_destroy(NULL);
    telos_value_release(zero_value);
    telos_value_release(two);
    telos_value_release(null_value);
    return 0;
}
