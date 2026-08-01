#ifndef TELOS_TOOL_H
#define TELOS_TOOL_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/registry.h>
#include <telos/value.h>

enum telos_execution_domain {
    TELOS_EXECUTION_CORE = 1,
    TELOS_EXECUTION_PROVIDER,
};

enum telos_policy_decision {
    TELOS_POLICY_ALLOW = 1,
    TELOS_POLICY_DENY,
    TELOS_POLICY_REQUIRE_APPROVAL,
};

struct telos_tool_context {
    const struct telos_cancel *cancel;
};

typedef bool (*telos_tool_execute_fn)(const struct telos_tool_context *context,
                                      const struct telos_value *arguments,
                                      struct telos_value **result,
                                      struct telos_error **error);

struct telos_tool_definition {
    const char *id;
    const struct telos_value *input_schema;
    const char *const *required_capabilities;
    size_t required_capability_count;
    telos_tool_execute_fn execute;
};

struct telos_policy_request {
    const char *tool_id;
    const struct telos_value *arguments;
    const char *const *required_capabilities;
    size_t required_capability_count;
};

typedef enum telos_policy_decision
(*telos_policy_evaluate_fn)(const struct telos_policy_request *request,
                            void *context);

struct telos_capability_broker;

struct telos_capability_broker *
telos_capability_broker_create(const char *const *available_capabilities,
                               size_t available_capability_count,
                               telos_policy_evaluate_fn evaluate,
                               void *evaluate_context,
                               struct telos_error **error);

void telos_capability_broker_destroy(struct telos_capability_broker *broker);

bool telos_tool_execute(const struct telos_registry_generation *generation,
                        struct telos_capability_broker *broker,
                        enum telos_execution_domain domain,
                        const char *tool_id,
                        const struct telos_value *arguments,
                        const struct telos_cancel *cancel,
                        struct telos_value **result,
                        struct telos_error **error);

#endif
