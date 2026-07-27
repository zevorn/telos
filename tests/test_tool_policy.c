#include <stdio.h>
#include <string.h>

#include <telos/tool.h>

struct policy_context {
    enum telos_policy_decision decision;
    unsigned int calls;
};

static enum telos_policy_decision evaluate(
    const struct telos_policy_request *request,
    void *context
)
{
    struct policy_context *policy = context;

    (void)request;
    policy->calls += 1;
    return policy->decision;
}

static bool echo(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    const char *text = telos_value_string(telos_value_get(arguments, "text"));

    (void)context;
    (void)error;
    *result = telos_value_new_string(text);
    return *result != NULL;
}

int main(void)
{
    struct telos_value *type_object = telos_value_new_string("object");
    struct telos_value *type_string = telos_value_new_string("string");
    const char *text_schema_keys[] = {"type"};
    const struct telos_value *text_schema_values[] = {type_string};
    struct telos_value *text_schema = telos_value_new_object(
        text_schema_keys,
        text_schema_values,
        1
    );
    const char *property_keys[] = {"text"};
    const struct telos_value *property_values[] = {text_schema};
    struct telos_value *properties = telos_value_new_object(
        property_keys,
        property_values,
        1
    );
    struct telos_value *required_text = telos_value_new_string("text");
    const struct telos_value *required_values[] = {required_text};
    struct telos_value *required = telos_value_new_array(required_values, 1);
    const char *schema_keys[] = {"type", "properties", "required"};
    const struct telos_value *schema_values[] = {
        type_object,
        properties,
        required,
    };
    struct telos_value *schema = telos_value_new_object(
        schema_keys,
        schema_values,
        3
    );
    const char *capabilities[] = {"filesystem.read"};
    const struct telos_tool_definition definition = {
        .id = "echo",
        .input_schema = schema,
        .required_capabilities = capabilities,
        .required_capability_count = 1,
        .execute = echo,
    };
    struct telos_registry *registry = telos_registry_create(
        capabilities,
        1,
        NULL
    );
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "fixture.tools", NULL);
    const struct telos_extension_descriptor descriptor = {
        .id = "echo",
        .kind = TELOS_EXTENSION_TOOL,
        .required_capabilities = capabilities,
        .required_capability_count = 1,
        .implementation = &definition,
    };
    struct policy_context policy = {.decision = TELOS_POLICY_ALLOW};
    struct telos_capability_broker *broker = telos_capability_broker_create(
        capabilities,
        1,
        evaluate,
        &policy,
        NULL
    );
    struct telos_value *text = telos_value_new_string("hello");
    const char *argument_keys[] = {"text"};
    const struct telos_value *argument_values[] = {text};
    struct telos_value *arguments = telos_value_new_object(
        argument_keys,
        argument_values,
        1
    );
    struct telos_registry_generation *generation;
    struct telos_value *result = NULL;
    bool passed;

    passed = telos_registry_transaction_add(
        transaction,
        &descriptor,
        NULL
    ) && telos_registry_transaction_commit(transaction, NULL);
    generation = telos_registry_acquire(registry);
    passed = passed
        && telos_tool_execute(
            generation,
            broker,
            TELOS_EXECUTION_CORE,
            "echo",
            arguments,
            NULL,
            &result,
            NULL
        )
        && result != NULL
        && strcmp(telos_value_string(result), "hello") == 0
        && policy.calls == 1;

    telos_value_release(result);
    telos_registry_generation_release(generation);
    telos_value_release(arguments);
    telos_value_release(text);
    telos_capability_broker_destroy(broker);
    telos_registry_destroy(registry);
    telos_value_release(schema);
    telos_value_release(required);
    telos_value_release(required_text);
    telos_value_release(properties);
    telos_value_release(text_schema);
    telos_value_release(type_string);
    telos_value_release(type_object);
    if (!passed) {
        fputs("authorized Tool did not execute through Core\n", stderr);
        return 1;
    }
    return 0;
}
