#define _XOPEN_SOURCE 700

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <telos/agent.h>
#include <telos/plugin_process.h>
#include <telos/prompt.h>
#include <telos/store.h>

static struct telos_plugin_process *tool_process;

struct fixture_provider {
    size_t rounds;
};

static struct telos_value *tool_schema(void)
{
    struct telos_value *string_type = telos_value_new_string("string");
    const char *string_keys[] = {"type"};
    const struct telos_value *string_values[] = {string_type};
    struct telos_value *text_schema = telos_value_new_object(
        string_keys,
        string_values,
        1
    );
    const char *property_keys[] = {"text"};
    const struct telos_value *property_values[] = {text_schema};
    struct telos_value *properties = telos_value_new_object(
        property_keys,
        property_values,
        1
    );
    struct telos_value *required_name = telos_value_new_string("text");
    const struct telos_value *required_values[] = {required_name};
    struct telos_value *required = telos_value_new_array(required_values, 1);
    struct telos_value *object_type = telos_value_new_string("object");
    const char *keys[] = {"type", "properties", "required"};
    const struct telos_value *values[] = {
        object_type,
        properties,
        required,
    };
    struct telos_value *result = telos_value_new_object(keys, values, 3);

    telos_value_release(object_type);
    telos_value_release(required);
    telos_value_release(required_name);
    telos_value_release(properties);
    telos_value_release(text_schema);
    telos_value_release(string_type);
    return result;
}

static bool process_echo(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    return telos_plugin_process_request(
        tool_process,
        "echo",
        arguments,
        1000,
        context->cancel,
        result,
        error
    );
}

static enum telos_policy_decision allow_echo(
    const struct telos_policy_request *request,
    void *context
)
{
    size_t *authorizations = context;

    assert(strcmp(request->tool_id, "dev.zevorn.process-echo") == 0);
    *authorizations += 1;
    return TELOS_POLICY_ALLOW;
}

static bool provider_dispatch(
    const struct telos_provider_request *request,
    telos_provider_event_fn emit,
    void *emit_context,
    void *provider_context,
    struct telos_error **error
)
{
    struct fixture_provider *provider = provider_context;
    struct telos_provider_event event = {0};

    provider->rounds += 1;
    if (provider->rounds == 1) {
        struct telos_value *text = telos_value_new_string("from-process");
        const char *keys[] = {"text"};
        const struct telos_value *values[] = {text};
        struct telos_value *arguments = telos_value_new_object(keys, values, 1);

        assert(strstr(request->instructions, "USER GUIDANCE") != NULL);
        event.kind = TELOS_PROVIDER_TOOL_CALL_COMPLETED;
        event.call_id = "process-call-1";
        event.name = "dev.zevorn.process-echo";
        event.payload = arguments;
        assert(emit(&event, emit_context, error));
        telos_value_release(arguments);
        telos_value_release(text);
    } else {
        const struct telos_value *item = telos_value_at(request->items, 0);
        const struct telos_value *output = telos_value_get(item, "output");

        assert(telos_value_count(request->items) == 1);
        assert(strcmp(
            telos_value_string(telos_value_get(output, "text")),
            "from-process"
        ) == 0);
        event.kind = TELOS_PROVIDER_TEXT_DELTA;
        event.delta = "process tool completed";
        assert(emit(&event, emit_context, error));
    }
    event = (struct telos_provider_event) {
        .kind = TELOS_PROVIDER_RESPONSE_COMPLETED,
    };
    return emit(&event, emit_context, error);
}

static struct telos_event *event(
    uint64_t sequence,
    const char *type,
    const struct telos_value *payload
)
{
    struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = telos_id_generate(),
        .session_id = {.high = 1, .low = 2},
        .correlation_id = {.high = 3, .low = 4},
        .causation_id = {.high = 5, .low = sequence - 1},
        .type = type,
        .source = "core:agent-loop",
        .timestamp_milliseconds = (int64_t)sequence,
        .payload = payload,
    };

    return telos_event_create(&spec, NULL);
}

int main(int argc, char **argv)
{
    const struct telos_prompt_fragment fragments[] = {
        {
            .slot = TELOS_PROMPT_USER_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_USER,
            .priority = 0,
            .byte_budget = 128,
            .source = "~/.telos/AGENTS.md",
            .content = "USER GUIDANCE",
        },
        {
            .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .priority = 0,
            .byte_budget = 128,
            .source = "./AGENTS.md",
            .content = "PROJECT GUIDANCE",
        },
    };
    struct telos_prompt_snapshot *prompt;
    struct telos_value *schema = tool_schema();
    struct telos_tool_definition tool = {
        .id = "dev.zevorn.process-echo",
        .input_schema = schema,
        .execute = process_echo,
    };
    struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.process-echo",
        .plugin_id = "dev.zevorn.process-fixture",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &tool,
    };
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_registry_transaction *transaction;
    struct telos_registry_generation *generation;
    size_t authorizations = 0;
    struct telos_capability_broker *broker;
    struct fixture_provider provider = {0};
    struct telos_agent_options options;
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *provider_options = telos_value_new_object(
        NULL,
        NULL,
        0
    );
    struct telos_provider_request request;
    struct telos_agent_result result = {0};
    struct telos_error *error = NULL;
    char state_path[] = "/tmp/telos-scenario-XXXXXX";
    int state_descriptor;
    struct telos_event_store *store;
    struct telos_value *sensitive;
    struct telos_event *started;
    struct telos_event *completed;

    assert(argc == 2);
    tool_process = telos_plugin_process_spawn(argv[1], &error);
    assert(tool_process != NULL);
    prompt = telos_prompt_snapshot_create(fragments, 2, &error);
    assert(prompt != NULL);
    assert(
        strstr(
            telos_prompt_snapshot_content(prompt),
            "USER GUIDANCE"
        )
        < strstr(
            telos_prompt_snapshot_content(prompt),
            "PROJECT GUIDANCE"
        )
    );
    transaction = telos_registry_transaction_begin(
        registry,
        "dev.zevorn.process-fixture",
        NULL
    );
    assert(telos_registry_transaction_add(transaction, &descriptor, NULL));
    assert(telos_registry_transaction_commit(transaction, NULL));
    generation = telos_registry_acquire(registry);
    broker = telos_capability_broker_create(
        NULL,
        0,
        allow_echo,
        &authorizations,
        NULL
    );
    options = (struct telos_agent_options) {
        .registry_generation = generation,
        .capability_broker = broker,
        .dispatch = provider_dispatch,
        .provider_context = &provider,
        .maximum_provider_rounds = 4,
    };
    request = (struct telos_provider_request) {
        .instructions = telos_prompt_snapshot_content(prompt),
        .items = items,
        .tools = tools,
        .options = provider_options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    assert(telos_agent_run(&options, &request, NULL, &result, &error));
    assert(strcmp(result.text, "process tool completed") == 0);
    assert(result.provider_rounds == 2);
    assert(result.tool_calls == 1);
    assert(authorizations == 1);

    state_descriptor = mkstemp(state_path);
    assert(state_descriptor >= 0);
    close(state_descriptor);
    unlink(state_path);
    store = telos_markdown_store_create(state_path, &error);
    sensitive = telos_value_new_sensitive("must-not-leak");
    started = event(1, "response.started", sensitive);
    completed = event(2, "response.completed", sensitive);
    assert(telos_event_store_append(store, started, &error));
    assert(telos_event_store_append(store, completed, &error));
    telos_event_release(completed);
    telos_event_release(started);
    telos_value_release(sensitive);
    telos_event_store_destroy(store);
    store = telos_markdown_store_create(state_path, &error);
    assert(telos_event_store_count(store) == 2);
    telos_event_store_destroy(store);
    unlink(state_path);

    assert(telos_plugin_process_shutdown(tool_process, 1000, &error));
    telos_plugin_process_destroy(tool_process);
    telos_agent_result_clear(&result);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_value_release(schema);
    telos_prompt_snapshot_release(prompt);
    return 0;
}
