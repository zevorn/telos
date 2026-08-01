#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/agent.h>

struct provider_fixture {
    size_t dispatches;
};

struct observer_fixture {
    size_t provider_events;
    size_t tools_started;
    size_t tools_completed;
};

static bool observe(const struct telos_agent_event *event,
                    void *context,
                    struct telos_error **error)
{
    struct observer_fixture *fixture = context;

    (void)error;
    assert(event != NULL);
    assert(event->provider_round == 1 || event->provider_round == 2);
    if (event->kind == TELOS_AGENT_PROVIDER_EVENT) {
        assert(event->provider_event != NULL);
        fixture->provider_events += 1;
    } else if (event->kind == TELOS_AGENT_TOOL_STARTED) {
        assert(strcmp(event->tool_name, "dev.zevorn.echo") == 0);
        assert(strcmp(event->tool_call_id, "call-1") == 0 ||
               strcmp(event->tool_call_id, "call-2") == 0);
        fixture->tools_started += 1;
    } else if (event->kind == TELOS_AGENT_TOOL_COMPLETED) {
        assert(strcmp(event->tool_name, "dev.zevorn.echo") == 0);
        assert(strcmp(event->tool_call_id, "call-1") == 0 ||
               strcmp(event->tool_call_id, "call-2") == 0);
        assert(event->tool_result != NULL);
        assert(event->tool_error == NULL);
        fixture->tools_completed += 1;
    } else {
        abort();
    }
    return true;
}

static void assert_string_field(const struct telos_value *object,
                                const char *key,
                                const char *expected)
{
    assert(strcmp(telos_value_string(telos_value_get(object, key)), expected) ==
           0);
}

static struct telos_value *schema(void)
{
    struct telos_value *type = telos_value_new_string("object");
    const char *keys[] = {"type"};
    const struct telos_value *values[] = {type};
    struct telos_value *result = telos_value_new_object(keys, values, 1);

    telos_value_release(type);
    return result;
}

static bool echo_execute(const struct telos_tool_context *context,
                         const struct telos_value *arguments,
                         struct telos_value **result,
                         struct telos_error **error)
{
    (void)context;
    (void)error;
    *result = telos_value_retain(arguments);
    return true;
}

static enum telos_policy_decision
allow(const struct telos_policy_request *request, void *context)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_ALLOW;
}

static bool dispatch(const struct telos_provider_request *request,
                     telos_provider_event_fn emit,
                     void *emit_context,
                     void *provider_context,
                     struct telos_error **error)
{
    struct provider_fixture *fixture = provider_context;
    struct telos_provider_event event = {0};

    fixture->dispatches += 1;
    if (fixture->dispatches == 1) {
        struct telos_value *first = telos_value_new_object(NULL, NULL, 0);
        struct telos_value *second = telos_value_new_object(NULL, NULL, 0);

        assert(telos_value_count(request->items) == 1);
        event.kind = TELOS_PROVIDER_TOOL_CALL_COMPLETED;
        event.call_id = "call-1";
        event.name = "dev.zevorn.echo";
        event.payload = first;
        if (!emit(&event, emit_context, error)) {
            telos_value_release(second);
            telos_value_release(first);
            return false;
        }
        event.call_id = "call-2";
        event.payload = second;
        if (!emit(&event, emit_context, error)) {
            telos_value_release(second);
            telos_value_release(first);
            return false;
        }
        telos_value_release(second);
        telos_value_release(first);
        event = (struct telos_provider_event){
            .kind = TELOS_PROVIDER_RESPONSE_COMPLETED,
        };
        return emit(&event, emit_context, error);
    }

    assert(telos_value_count(request->items) == 5);
    assert_string_field(telos_value_at(request->items, 0), "content",
                        "use echo");
    assert_string_field(telos_value_at(request->items, 1), "type",
                        "function_call");
    assert_string_field(telos_value_at(request->items, 1), "arguments", "{}");
    assert_string_field(telos_value_at(request->items, 2), "type",
                        "function_call");
    assert_string_field(telos_value_at(request->items, 3), "type",
                        "function_call_output");
    assert_string_field(telos_value_at(request->items, 3), "output", "{}");
    assert_string_field(telos_value_at(request->items, 4), "type",
                        "function_call_output");
    event.kind = TELOS_PROVIDER_TEXT_DELTA;
    event.delta = "both tools completed";
    if (!emit(&event, emit_context, error)) {
        return false;
    }
    event = (struct telos_provider_event){
        .kind = TELOS_PROVIDER_RESPONSE_COMPLETED,
    };
    return emit(&event, emit_context, error);
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
        telos_registry_transaction_begin(registry, "dev.zevorn.fixture", NULL);
    struct telos_registry_generation *generation;
    struct telos_capability_broker *broker =
        telos_capability_broker_create(NULL, 0, allow, NULL, NULL);
    struct provider_fixture fixture = {0};
    struct observer_fixture observer = {0};
    struct telos_agent_options options;
    struct telos_value *role = telos_value_new_string("user");
    struct telos_value *content = telos_value_new_string("use echo");
    const char *item_keys[] = {"role", "content"};
    const struct telos_value *item_fields[] = {role, content};
    struct telos_value *item =
        telos_value_new_object(item_keys, item_fields, 2);
    const struct telos_value *item_values[] = {item};
    struct telos_value *items = telos_value_new_array(item_values, 1);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *provider_options =
        telos_value_new_object(NULL, NULL, 0);
    struct telos_provider_request request = {
        .instructions = "Use the echo Tool twice",
        .items = items,
        .tools = tools,
        .options = provider_options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_agent_result result = {0};
    struct telos_error *error = NULL;

    assert(telos_registry_transaction_add(transaction, &descriptor, NULL));
    assert(telos_registry_transaction_commit(transaction, NULL));
    generation = telos_registry_acquire(registry);
    options = (struct telos_agent_options){
        .registry_generation = generation,
        .capability_broker = broker,
        .dispatch = dispatch,
        .provider_context = &fixture,
        .observe = observe,
        .observe_context = &observer,
        .maximum_provider_rounds = 4,
    };

    assert(telos_agent_run(&options, &request, NULL, &result, &error));
    assert(error == NULL);
    assert(strcmp(result.text, "both tools completed") == 0);
    assert(result.provider_rounds == 2);
    assert(result.tool_calls == 2);
    assert(observer.provider_events == 5);
    assert(observer.tools_started == 2);
    assert(observer.tools_completed == 2);

    telos_agent_result_clear(&result);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(item);
    telos_value_release(content);
    telos_value_release(role);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_value_release(input_schema);
    return 0;
}
