#ifndef TELOS_PROVIDER_H
#define TELOS_PROVIDER_H

#include <stddef.h>

#include <telos/error.h>
#include <telos/value.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef bool (*telos_provider_event_fn)(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
