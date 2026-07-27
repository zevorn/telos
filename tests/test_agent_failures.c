#include <assert.h>

#include <telos/agent.h>

static enum telos_policy_decision allow(
    const struct telos_policy_request *request,
    void *context
)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_ALLOW;
}

static bool incomplete(
    const struct telos_provider_request *request,
    telos_provider_event_fn emit,
    void *emit_context,
    void *provider_context,
    struct telos_error **error
)
{
    (void)request;
    (void)emit;
    (void)emit_context;
    (void)provider_context;
    (void)error;
    return true;
}

int main(void)
{
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_registry_generation *generation =
        telos_registry_acquire(registry);
    struct telos_capability_broker *broker =
        telos_capability_broker_create(NULL, 0, allow, NULL, NULL);
    struct telos_agent_options options = {
        .registry_generation = generation,
        .capability_broker = broker,
        .dispatch = incomplete,
        .maximum_provider_rounds = 1,
    };
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *provider_options = telos_value_new_object(
        NULL,
        NULL,
        0
    );
    struct telos_provider_request request = {
        .instructions = "fixture",
        .items = items,
        .tools = tools,
        .options = provider_options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_agent_result result = {0};
    struct telos_cancel *cancel = telos_cancel_create();
    struct telos_error *error = NULL;

    assert(!telos_agent_run(
        &options,
        &request,
        NULL,
        &result,
        &error
    ));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PROTOCOL);
    telos_error_release(error);
    error = NULL;

    assert(telos_cancel_request(cancel));
    assert(!telos_agent_run(
        &options,
        &request,
        cancel,
        &result,
        &error
    ));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_CANCELLED);
    telos_error_release(error);

    telos_cancel_release(cancel);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    return 0;
}
