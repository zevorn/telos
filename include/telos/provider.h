#ifndef TELOS_PROVIDER_H
#define TELOS_PROVIDER_H

#include <telos/types.h>

#include <telos/error.h>
#include <telos/value.h>

enum telos_provider_state_mode {
    TELOS_PROVIDER_STATE_LOCAL = 1,
    TELOS_PROVIDER_STATE_REMOTE,
};

struct telos_provider_request {
    const char *instructions;
    const struct telos_value *items;
    const struct telos_value *tools;
    const struct telos_value *options;
    enum telos_provider_state_mode state_mode;
    const char *previous_response_id;
};

typedef struct telos_provider_request telos_provider_request;

enum telos_provider_event_kind {
    TELOS_PROVIDER_RESPONSE_STARTED = 1,
    TELOS_PROVIDER_OUTPUT_ITEM_ADDED,
    TELOS_PROVIDER_TEXT_DELTA,
    TELOS_PROVIDER_TOOL_CALL_STARTED,
    TELOS_PROVIDER_TOOL_ARGUMENT_DELTA,
    TELOS_PROVIDER_TOOL_CALL_COMPLETED,
    TELOS_PROVIDER_REASONING_ITEM,
    TELOS_PROVIDER_USAGE_UPDATE,
    TELOS_PROVIDER_RESPONSE_COMPLETED,
    TELOS_PROVIDER_ERROR,
};

struct telos_provider_event {
    enum telos_provider_event_kind kind;
    const char *response_id;
    const char *item_id;
    const char *call_id;
    const char *name;
    const char *delta;
    const struct telos_value *payload;
};

typedef struct telos_provider_event telos_provider_event;

typedef bool (*telos_provider_event_fn)(const telos_provider_event *event,
                                        void *context,
                                        struct telos_error **error);

typedef bool (*telos_provider_dispatch_fn)(const telos_provider_request *req,
                                           telos_provider_event_fn emit,
                                           void *emit_context,
                                           void *provider_context,
                                           struct telos_error **error);

struct telos_provider_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_provider_dispatch_fn dispatch;
};

#endif
