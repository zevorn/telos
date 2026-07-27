#include <assert.h>
#include <string.h>

#include <telos/agent.h>

struct fixture {
    size_t dispatches;
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
    void *emit_context,
    struct telos_error **error
)
{
    const struct telos_provider_event event = {
        .kind = TELOS_PROVIDER_RESPONSE_COMPLETED,
    };

    return emit(&event, emit_context, error);
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

    fixture->dispatches += 1;
    if (fixture->dispatches == 1) {
        struct telos_value *arguments =
            telos_value_new_object(NULL, NULL, 0);
        struct telos_provider_event event = {
            .kind = TELOS_PROVIDER_RESPONSE_STARTED,
            .response_id = "resp_tool",
        };

        assert(request->previous_response_id == NULL);
        assert(emit(&event, emit_context, error));
        event = (struct telos_provider_event) {
            .kind = TELOS_PROVIDER_TOOL_CALL_COMPLETED,
            .call_id = "call_remote",
            .name = "dev.zevorn.echo",
            .payload = arguments,
        };
        assert(emit(&event, emit_context, error));
        telos_value_release(arguments);
        return emit_completed(emit, emit_context, error);
    }

    assert(strcmp(request->previous_response_id, "resp_tool") == 0);
    assert(telos_value_count(request->items) == 1);
    assert(strcmp(
        telos_value_string(
            telos_value_get(telos_value_at(request->items, 0), "type")
        ),
        "function_call_output"
    ) == 0);
    assert(strcmp(
        telos_value_string(
            telos_value_get(telos_value_at(request->items, 0), "output")
        ),
        "{}"
    ) == 0);
    {
        struct telos_provider_event event = {
            .kind = TELOS_PROVIDER_TEXT_DELTA,
            .delta = "remote complete",
        };

        assert(emit(&event, emit_context, error));
    }
    return emit_completed(emit, emit_context, error);
}

int main(void)
{
    struct telos_value *input_schema = schema();
    struct telos_tool_definition tool = {
        .id = "dev.zevorn.echo",
        .input_schema = input_schema,
        .execute = echo_execute,
    };
    struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.echo",
        .plugin_id = "dev.zevorn.fixture",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &tool,
    };
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(
            registry,
            "dev.zevorn.fixture",
            NULL
        );
    struct telos_registry_generation *generation;
    struct telos_capability_broker *broker =
        telos_capability_broker_create(NULL, 0, allow, NULL, NULL);
    struct fixture fixture = {0};
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *provider_options =
        telos_value_new_object(NULL, NULL, 0);
    const struct telos_provider_request request = {
        .instructions = "remote mode",
        .items = items,
        .tools = tools,
        .options = provider_options,
        .state_mode = TELOS_PROVIDER_STATE_REMOTE,
    };
    struct telos_agent_result result = {0};
    struct telos_error *error = NULL;

    assert(telos_registry_transaction_add(
        transaction,
        &descriptor,
        NULL
    ));
    assert(telos_registry_transaction_commit(transaction, NULL));
    generation = telos_registry_acquire(registry);
    {
        const struct telos_agent_options options = {
            .registry_generation = generation,
            .capability_broker = broker,
            .dispatch = dispatch,
            .provider_context = &fixture,
            .maximum_provider_rounds = 3,
        };

        assert(telos_agent_run(
            &options,
            &request,
            NULL,
            &result,
            &error
        ));
    }
    assert(error == NULL);
    assert(strcmp(result.text, "remote complete") == 0);
    assert(result.provider_rounds == 2);
    assert(result.tool_calls == 1);

    telos_agent_result_clear(&result);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_value_release(input_schema);
    return 0;
}
