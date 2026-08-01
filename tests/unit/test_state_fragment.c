#include <assert.h>
#include <errno.h>

#include <telos/state_fragment.h>

static enum telos_fragment_result
handle(const struct telos_state_fragment_context *context,
       const struct telos_event *event,
       struct telos_error **error)
{
    unsigned int *calls = context->plugin_context;

    (void)event;
    (void)error;
    *calls += 1;
    return TELOS_FRAGMENT_COMPLETED;
}

static enum telos_fragment_result
invalid_handle(const struct telos_state_fragment_context *context,
               const struct telos_event *event,
               struct telos_error **error)
{
    (void)context;
    (void)event;
    (void)error;
    return (enum telos_fragment_result)99;
}

static enum telos_fragment_result
zero_handle(const struct telos_state_fragment_context *context,
            const struct telos_event *event,
            struct telos_error **error)
{
    (void)context;
    (void)event;
    (void)error;
    return (enum telos_fragment_result)0;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

int main(void)
{
    const char *accepted[] = {"context.extend"};
    const char *invalid_accepted[] = {""};
    const char *null_accepted[] = {NULL};
    const char *multiple_accepted[] = {"other.event", "context.extend"};
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

    assert(!telos_state_fragment_validate(NULL, &error));
    clear_error(&error);
    assert(!telos_state_fragment_validate(NULL, NULL));
    {
        struct telos_state_fragment invalid = fragment;

        invalid.id = NULL;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.id = "";
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.slot = (enum telos_extension_slot)0;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.slot = (enum telos_extension_slot)99;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.accepted_event_types = NULL;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.accepted_event_type_count = 0;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.accepted_event_types = invalid_accepted;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid.accepted_event_types = null_accepted;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.handle = NULL;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
        invalid = fragment;
        invalid.timeout_milliseconds = 0;
        assert(!telos_state_fragment_validate(&invalid, &error));
        clear_error(&error);
    }
    assert(telos_state_fragment_validate(&fragment, &error));
    {
        struct telos_state_fragment multiple = fragment;

        multiple.accepted_event_types = multiple_accepted;
        multiple.accepted_event_type_count = 2;
        assert(telos_state_fragment_execute(&multiple, TELOS_SLOT_CONTEXT_BUILD,
                                            &context, event, &result, &error));
        calls = 0;
    }
    assert(!telos_state_fragment_execute(&fragment, TELOS_SLOT_CONTEXT_BUILD,
                                         NULL, event, &result, &error));
    clear_error(&error);
    assert(!telos_state_fragment_execute(&fragment, TELOS_SLOT_CONTEXT_BUILD,
                                         &context, NULL, &result, &error));
    clear_error(&error);
    assert(!telos_state_fragment_execute(&fragment, TELOS_SLOT_CONTEXT_BUILD,
                                         &context, event, NULL, &error));
    clear_error(&error);
    assert(telos_state_fragment_execute(&fragment, TELOS_SLOT_CONTEXT_BUILD,
                                        &context, event, &result, &error));
    assert(result == TELOS_FRAGMENT_COMPLETED);
    assert(calls == 1);
    assert(!telos_state_fragment_execute(&fragment, TELOS_SLOT_FINAL_COMMIT,
                                         &context, event, &result, &error));
    assert(error != NULL);
    clear_error(&error);
    {
        struct telos_event_spec rejected_spec = spec;
        struct telos_event *rejected_event;

        rejected_spec.type = "other.event";
        rejected_event = telos_event_create(&rejected_spec, NULL);
        assert(!telos_state_fragment_execute(&fragment,
                                             TELOS_SLOT_CONTEXT_BUILD, &context,
                                             rejected_event, &result, &error));
        assert(telos_error_code(error) == ENOMSG);
        clear_error(&error);
        telos_event_release(rejected_event);
    }
    {
        struct telos_cancel *cancel = telos_cancel_create();
        struct telos_state_fragment_context cancelled = context;

        assert(telos_cancel_request(cancel));
        cancelled.cancel = cancel;
        assert(
            !telos_state_fragment_execute(&fragment, TELOS_SLOT_CONTEXT_BUILD,
                                          &cancelled, event, &result, &error));
        assert(telos_error_code(error) == ECANCELED);
        clear_error(&error);
        telos_cancel_release(cancel);
    }
    {
        struct telos_state_fragment invalid = fragment;

        invalid.handle = invalid_handle;
        assert(!telos_state_fragment_execute(&invalid, TELOS_SLOT_CONTEXT_BUILD,
                                             &context, event, &result, &error));
        assert(telos_error_code(error) == EPROTO);
        clear_error(&error);
        invalid.handle = zero_handle;
        assert(!telos_state_fragment_execute(&invalid, TELOS_SLOT_CONTEXT_BUILD,
                                             &context, event, &result, NULL));
    }
    telos_event_release(event);
    telos_value_release(payload);
    return 0;
}
