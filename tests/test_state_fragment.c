#include <assert.h>

#include <telos/state_fragment.h>

static enum telos_fragment_result handle(
    const struct telos_state_fragment_context *context,
    const struct telos_event *event,
    struct telos_error **error
)
{
    unsigned int *calls = context->plugin_context;

    (void)event;
    (void)error;
    *calls += 1;
    return TELOS_FRAGMENT_COMPLETED;
}

int main(void)
{
    const char *accepted[] = {"context.extend"};
    struct telos_state_fragment fragment = {
        .id = "dev.zevorn.context-fragment",
        .slot = TELOS_SLOT_CONTEXT_BUILD,
        .accepted_event_types = accepted,
        .accepted_event_type_count = 1,
        .handle = handle,
        .timeout_milliseconds = 100,
    };
    struct telos_value *payload = telos_value_new_null();
    struct telos_event_spec spec = {
        .sequence = 1,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = "context.extend",
        .source = "test",
        .timestamp_milliseconds = 1,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);
    unsigned int calls = 0;
    struct telos_state_fragment_context context = {
        .plugin_context = &calls,
    };
    enum telos_fragment_result result = 0;
    struct telos_error *error = NULL;

    assert(telos_state_fragment_validate(&fragment, &error));
    assert(telos_state_fragment_execute(
        &fragment,
        TELOS_SLOT_CONTEXT_BUILD,
        &context,
        event,
        &result,
        &error
    ));
    assert(result == TELOS_FRAGMENT_COMPLETED);
    assert(calls == 1);
    assert(!telos_state_fragment_execute(
        &fragment,
        TELOS_SLOT_FINAL_COMMIT,
        &context,
        event,
        &result,
        &error
    ));
    assert(error != NULL);
    telos_error_release(error);
    telos_event_release(event);
    telos_value_release(payload);
    return 0;
}
