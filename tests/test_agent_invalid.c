#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <telos/agent.h>

enum dispatch_mode {
    DISPATCH_EMPTY,
    DISPATCH_NULL_EVENT,
    DISPATCH_STARTED_NULL,
    DISPATCH_STARTED_EMPTY,
    DISPATCH_STARTED_DUPLICATE,
    DISPATCH_TEXT_NULL,
    DISPATCH_CALL_NULL_ID,
    DISPATCH_CALL_EMPTY_ID,
    DISPATCH_CALL_NULL_NAME,
    DISPATCH_CALL_EMPTY_NAME,
    DISPATCH_CALL_NULL_PAYLOAD,
    DISPATCH_CALL_DUPLICATE,
    DISPATCH_LATE,
    DISPATCH_PROVIDER_ERROR,
    DISPATCH_UNKNOWN,
    DISPATCH_FALSE,
    DISPATCH_FALSE_ERROR,
    DISPATCH_MISSING_TOOL,
    DISPATCH_SILENT_TOOL,
    DISPATCH_ERROR_TOOL,
    DISPATCH_REMOTE_NO_ID,
    DISPATCH_ENDLESS_TOOL,
};

struct fixture {
    enum dispatch_mode mode;
};

static struct telos_value *schema(void)
{
    struct telos_value *type = telos_value_new_string("object");
    const char *keys[] = {"type"};
    const struct telos_value *values[] = {type};
    struct telos_value *result = telos_value_new_object(keys, values, 1);

    telos_value_release(type);
    return result;
}

static bool echo_execute(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)error;
    *result = telos_value_retain(arguments);
    return true;
}

static bool silent_failure(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)arguments;
    (void)result;
    (void)error;
    return false;
}

static bool explicit_failure(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)arguments;
    (void)result;
    *error = telos_error_create(
        TELOS_ERROR_DOMAIN_PLUGIN,
        EIO,
        "fixture tool failure",
        NULL
    );
    return false;
}

static enum telos_policy_decision allow(
    const struct telos_policy_request *request,
    void *context
)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_ALLOW;
}

static bool emit_completed(
    telos_provider_event_fn emit,
    void *context,
    struct telos_error **error
)
{
    const struct telos_provider_event event = {
        .kind = TELOS_PROVIDER_RESPONSE_COMPLETED,
    };

    return emit(&event, context, error);
}

static bool emit_call(
    const char *call_id,
    const char *name,
    const struct telos_value *payload,
    telos_provider_event_fn emit,
    void *context,
    struct telos_error **error
)
{
    const struct telos_provider_event event = {
        .kind = TELOS_PROVIDER_TOOL_CALL_COMPLETED,
        .call_id = call_id,
        .name = name,
        .payload = payload,
    };

    return emit(&event, context, error);
}

static bool dispatch(
    const struct telos_provider_request *request,
    telos_provider_event_fn emit,
    void *emit_context,
    void *provider_context,
    struct telos_error **error
)
{
    struct fixture *fixture = provider_context;
    struct telos_value *payload = telos_value_new_object(NULL, NULL, 0);
    struct telos_provider_event event = {0};
    bool result = false;

    (void)request;
    switch (fixture->mode) {
    case DISPATCH_EMPTY:
        result = emit_completed(emit, emit_context, error);
        break;
    case DISPATCH_NULL_EVENT:
        result = emit(NULL, emit_context, error);
        break;
    case DISPATCH_STARTED_NULL:
    case DISPATCH_STARTED_EMPTY:
        event.kind = TELOS_PROVIDER_RESPONSE_STARTED;
        event.response_id = fixture->mode == DISPATCH_STARTED_EMPTY ? "" : NULL;
        result = emit(&event, emit_context, error);
        break;
    case DISPATCH_STARTED_DUPLICATE:
        event.kind = TELOS_PROVIDER_RESPONSE_STARTED;
        event.response_id = "response-one";
        result = emit(&event, emit_context, error);
        if (result) {
            event.response_id = "response-two";
            result = emit(&event, emit_context, error);
        }
        break;
    case DISPATCH_TEXT_NULL:
        event.kind = TELOS_PROVIDER_TEXT_DELTA;
        result = emit(&event, emit_context, error);
        break;
    case DISPATCH_CALL_NULL_ID:
    case DISPATCH_CALL_EMPTY_ID:
    case DISPATCH_CALL_NULL_NAME:
    case DISPATCH_CALL_EMPTY_NAME:
    case DISPATCH_CALL_NULL_PAYLOAD:
        result = emit_call(
            fixture->mode == DISPATCH_CALL_NULL_ID
                ? NULL
                : fixture->mode == DISPATCH_CALL_EMPTY_ID ? "" : "call",
            fixture->mode == DISPATCH_CALL_NULL_NAME
                ? NULL
                : fixture->mode == DISPATCH_CALL_EMPTY_NAME ? "" : "echo",
            fixture->mode == DISPATCH_CALL_NULL_PAYLOAD ? NULL : payload,
            emit,
            emit_context,
            error
        );
        break;
    case DISPATCH_CALL_DUPLICATE:
        result = emit_call(
            "duplicate",
            "echo",
            payload,
            emit,
            emit_context,
            error
        );
        if (result) {
            result = emit_call(
                "duplicate",
                "echo",
                payload,
                emit,
                emit_context,
                error
            );
        }
        break;
    case DISPATCH_LATE:
        result = emit_completed(emit, emit_context, error);
        if (result) {
            event.kind = TELOS_PROVIDER_TEXT_DELTA;
            event.delta = "late";
            result = emit(&event, emit_context, error);
        }
        break;
    case DISPATCH_PROVIDER_ERROR:
        event.kind = TELOS_PROVIDER_ERROR;
        result = emit(&event, emit_context, error);
        break;
    case DISPATCH_UNKNOWN:
        event.kind = (enum telos_provider_event_kind)99;
        result = emit(&event, emit_context, error);
        if (result) {
            result = emit_completed(emit, emit_context, error);
        }
        break;
    case DISPATCH_FALSE:
        result = false;
        break;
    case DISPATCH_FALSE_ERROR:
        *error = telos_error_create(
            TELOS_ERROR_DOMAIN_IO,
            EIO,
            "fixture provider failure",
            NULL
        );
        result = false;
        break;
    case DISPATCH_MISSING_TOOL:
    case DISPATCH_SILENT_TOOL:
    case DISPATCH_ERROR_TOOL:
    case DISPATCH_REMOTE_NO_ID:
    case DISPATCH_ENDLESS_TOOL:
        result = emit_call(
            "call",
            fixture->mode == DISPATCH_MISSING_TOOL
                ? "missing"
                : fixture->mode == DISPATCH_SILENT_TOOL
                    ? "silent"
                    : fixture->mode == DISPATCH_ERROR_TOOL
                        ? "failure"
                        : "echo",
            payload,
            emit,
            emit_context,
            error
        );
        if (result) {
            result = emit_completed(emit, emit_context, error);
        }
        break;
    }
    telos_value_release(payload);
    return result;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

static void assert_invalid(
    const struct telos_agent_options *options,
    const struct telos_provider_request *request,
    struct telos_agent_result *result,
    struct telos_error **error
)
{
    assert(!telos_agent_run(options, request, NULL, result, error));
    assert(*error != NULL);
    assert(telos_error_domain(*error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    assert(result->text == NULL);
    assert(result->provider_rounds == 0);
    assert(result->tool_calls == 0);
    clear_error(error);
}

static void assert_dispatch_failure(
    enum dispatch_mode mode,
    enum telos_provider_state_mode state_mode,
    const struct telos_agent_options *base_options,
    const struct telos_provider_request *base_request,
    enum telos_error_domain expected_domain
)
{
    struct fixture fixture = {.mode = mode};
    struct telos_agent_options options = *base_options;
    struct telos_provider_request request = *base_request;
    struct telos_agent_result result = {0};
    struct telos_error *error = NULL;

    options.provider_context = &fixture;
    request.state_mode = state_mode;
    assert(!telos_agent_run(&options, &request, NULL, &result, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == expected_domain);
    clear_error(&error);
    telos_agent_result_clear(&result);
}

int main(void)
{
    struct telos_value *input_schema = schema();
    const struct telos_tool_definition definitions[] = {
        {
            .id = "echo",
            .input_schema = input_schema,
            .execute = echo_execute,
        },
        {
            .id = "silent",
            .input_schema = input_schema,
            .execute = silent_failure,
        },
        {
            .id = "failure",
            .input_schema = input_schema,
            .execute = explicit_failure,
        },
    };
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "fixture", NULL);
    struct telos_registry_generation *generation;
    struct telos_capability_broker *broker =
        telos_capability_broker_create(NULL, 0, allow, NULL, NULL);
    struct fixture fixture = {.mode = DISPATCH_EMPTY};
    struct telos_agent_options options = {
        .registry_generation = NULL,
        .capability_broker = broker,
        .dispatch = dispatch,
        .provider_context = &fixture,
        .maximum_provider_rounds = 1,
    };
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *provider_options =
        telos_value_new_object(NULL, NULL, 0);
    struct telos_value *null_value = telos_value_new_null();
    struct telos_provider_request request = {
        .items = items,
        .tools = tools,
        .options = provider_options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_agent_result result = {
        .text = (char *)(uintptr_t)1,
        .provider_rounds = 99,
        .tool_calls = 99,
    };
    struct telos_error *error = NULL;

    for (size_t index = 0; index < 3; ++index) {
        const struct telos_extension_descriptor descriptor = {
            .id = definitions[index].id,
            .plugin_id = "fixture",
            .kind = TELOS_EXTENSION_TOOL,
            .implementation = &definitions[index],
        };

        assert(telos_registry_transaction_add(
            transaction,
            &descriptor,
            NULL
        ));
    }
    assert(telos_registry_transaction_commit(transaction, NULL));
    generation = telos_registry_acquire(registry);
    options.registry_generation = generation;

    assert_invalid(NULL, &request, &result, &error);
    assert_invalid(&options, NULL, &result, &error);
    assert(!telos_agent_run(&options, &request, NULL, NULL, &error));
    clear_error(&error);
    {
        struct telos_agent_options invalid = options;

        invalid.registry_generation = NULL;
        assert_invalid(&invalid, &request, &result, &error);
        invalid = options;
        invalid.capability_broker = NULL;
        assert_invalid(&invalid, &request, &result, &error);
        invalid = options;
        invalid.dispatch = NULL;
        assert_invalid(&invalid, &request, &result, &error);
    }
    {
        struct telos_provider_request invalid = request;

        invalid.items = NULL;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.items = null_value;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.tools = NULL;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.tools = null_value;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.options = NULL;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.options = null_value;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.state_mode = (enum telos_provider_state_mode)0;
        assert_invalid(&options, &invalid, &result, &error);
        invalid = request;
        invalid.state_mode = (enum telos_provider_state_mode)99;
        assert_invalid(&options, &invalid, &result, &error);
    }

    assert(telos_agent_run(&options, &request, NULL, &result, &error));
    assert(error == NULL);
    assert(strcmp(result.text, "") == 0);
    assert(result.provider_rounds == 1);
    telos_agent_result_clear(&result);

    for (
        enum dispatch_mode mode = DISPATCH_NULL_EVENT;
        mode <= DISPATCH_PROVIDER_ERROR;
        mode = (enum dispatch_mode)(mode + 1)
    ) {
        assert_dispatch_failure(
            mode,
            TELOS_PROVIDER_STATE_LOCAL,
            &options,
            &request,
            mode == DISPATCH_TEXT_NULL
                ? TELOS_ERROR_DOMAIN_MEMORY
                : TELOS_ERROR_DOMAIN_PROTOCOL
        );
    }
    fixture.mode = DISPATCH_UNKNOWN;
    assert(telos_agent_run(&options, &request, NULL, &result, &error));
    assert(error == NULL);
    assert(strcmp(result.text, "") == 0);
    telos_agent_result_clear(&result);

    assert_dispatch_failure(
        DISPATCH_FALSE,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_PROTOCOL
    );
    assert_dispatch_failure(
        DISPATCH_FALSE_ERROR,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_IO
    );
    assert_dispatch_failure(
        DISPATCH_MISSING_TOOL,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_ARGUMENT
    );
    assert_dispatch_failure(
        DISPATCH_SILENT_TOOL,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_STATE
    );
    assert_dispatch_failure(
        DISPATCH_ERROR_TOOL,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_PLUGIN
    );
    assert_dispatch_failure(
        DISPATCH_REMOTE_NO_ID,
        TELOS_PROVIDER_STATE_REMOTE,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_PROTOCOL
    );
    assert_dispatch_failure(
        DISPATCH_ENDLESS_TOOL,
        TELOS_PROVIDER_STATE_LOCAL,
        &options,
        &request,
        TELOS_ERROR_DOMAIN_STATE
    );

    telos_agent_result_clear(NULL);
    telos_value_release(null_value);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_value_release(input_schema);
    return 0;
}
