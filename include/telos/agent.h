#ifndef TELOS_AGENT_H
#define TELOS_AGENT_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/cancel.h>
#include <telos/provider.h>
#include <telos/tool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*telos_provider_dispatch_fn)(
    const struct telos_provider_request *request,
    telos_provider_event_fn emit,
    void *emit_context,
    void *provider_context,
    struct telos_error **error
);

struct telos_agent_options {
    const struct telos_registry_generation *registry_generation;
    struct telos_capability_broker *capability_broker;
    telos_provider_dispatch_fn dispatch;
    void *provider_context;
    size_t maximum_provider_rounds;
};

struct telos_agent_result {
    char *text;
    size_t provider_rounds;
    size_t tool_calls;
};

bool telos_agent_run(
    const struct telos_agent_options *options,
    const struct telos_provider_request *request,
    const struct telos_cancel *cancel,
    struct telos_agent_result *result,
    struct telos_error **error
);

void telos_agent_result_clear(struct telos_agent_result *result);

#ifdef __cplusplus
}
#endif

#endif
