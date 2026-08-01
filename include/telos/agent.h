#ifndef TELOS_AGENT_H
#define TELOS_AGENT_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/provider.h>
#include <telos/tool.h>

#define TELOS_AGENT_DEFAULT_MAXIMUM_RESPONSE_BYTES (1024U * 1024U)

enum telos_agent_event_kind {
    TELOS_AGENT_PROVIDER_EVENT = 1,
    TELOS_AGENT_TOOL_STARTED,
    TELOS_AGENT_TOOL_COMPLETED,
    TELOS_AGENT_TOOL_FAILED,
};

struct telos_agent_event {
    enum telos_agent_event_kind kind;
    size_t provider_round;
    const struct telos_provider_event *provider_event;
    const char *tool_call_id;
    const char *tool_name;
    const struct telos_value *tool_result;
    const struct telos_error *tool_error;
};

typedef bool (*telos_agent_observe_fn)(const struct telos_agent_event *event,
                                       void *context,
                                       struct telos_error **error);

struct telos_agent_options {
    const struct telos_registry_generation *registry_generation;
    struct telos_capability_broker *capability_broker;
    telos_provider_dispatch_fn dispatch;
    void *provider_context;
    telos_agent_observe_fn observe;
    void *observe_context;
    size_t maximum_provider_rounds;
    size_t maximum_response_bytes;
};

struct telos_agent_result {
    char *text;
    size_t provider_rounds;
    size_t tool_calls;
};

bool telos_agent_run(const struct telos_agent_options *options,
                     const struct telos_provider_request *request,
                     const struct telos_cancel *cancel,
                     struct telos_agent_result *result,
                     struct telos_error **error);

void telos_agent_result_clear(struct telos_agent_result *result);

#endif
