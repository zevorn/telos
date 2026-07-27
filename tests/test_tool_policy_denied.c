#include <stdio.h>

#include <telos/tool.h>

static enum telos_policy_decision require_approval(
    const struct telos_policy_request *request,
    void *context
)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_REQUIRE_APPROVAL;
}

static bool should_not_run(
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
    return true;
}

int main(void)
{
    struct telos_value *schema = telos_value_new_object(NULL, NULL, 0);
    const struct telos_tool_definition definition = {
        .id = "danger",
        .input_schema = schema,
        .execute = should_not_run,
    };
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "fixture.tools", NULL);
    const struct telos_extension_descriptor descriptor = {
        .id = "danger",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &definition,
    };
    struct telos_capability_broker *broker = telos_capability_broker_create(
        NULL,
        0,
        require_approval,
        NULL,
        NULL
    );
    struct telos_value *arguments = telos_value_new_object(NULL, NULL, 0);
    struct telos_registry_generation *generation;
    struct telos_error *error = NULL;
    struct telos_value *result = NULL;

    telos_registry_transaction_add(transaction, &descriptor, NULL);
    telos_registry_transaction_commit(transaction, NULL);
    generation = telos_registry_acquire(registry);
    if (
        telos_tool_execute(
            generation,
            broker,
            TELOS_EXECUTION_PROVIDER,
            "danger",
            arguments,
            NULL,
            &result,
            &error
        )
        || error == NULL
    ) {
        fputs("Provider directly invoked a local Tool\n", stderr);
        return 1;
    }
    telos_error_release(error);
    error = NULL;
    if (
        telos_tool_execute(
            generation,
            broker,
            TELOS_EXECUTION_CORE,
            "danger",
            arguments,
            NULL,
            &result,
            &error
        )
        || error == NULL
        || result != NULL
    ) {
        fputs("approval-required Tool executed without approval\n", stderr);
        return 1;
    }

    telos_error_release(error);
    telos_registry_generation_release(generation);
    telos_value_release(arguments);
    telos_capability_broker_destroy(broker);
    telos_registry_destroy(registry);
    telos_value_release(schema);
    return 0;
}
