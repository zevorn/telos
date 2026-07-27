#ifndef TELOS_STATE_FRAGMENT_H
#define TELOS_STATE_FRAGMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/cancel.h>
#include <telos/error.h>
#include <telos/event.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_extension_slot {
    TELOS_SLOT_INPUT_PREPARE = 1,
    TELOS_SLOT_CONTEXT_BUILD,
    TELOS_SLOT_PROVIDER_DISPATCH,
    TELOS_SLOT_RESPONSE_PROCESS,
    TELOS_SLOT_TOOL_EXECUTE,
    TELOS_SLOT_TOOL_COLLECT,
    TELOS_SLOT_FINAL_COMMIT,
};

enum telos_fragment_result {
    TELOS_FRAGMENT_COMPLETED = 1,
    TELOS_FRAGMENT_PENDING,
    TELOS_FRAGMENT_RETRYABLE_ERROR,
    TELOS_FRAGMENT_FATAL_ERROR,
};

struct telos_state_fragment_context {
    const struct telos_cancel *cancel;
    void *plugin_context;
};

typedef enum telos_fragment_result (*telos_state_fragment_handler_fn)(
    const struct telos_state_fragment_context *context,
    const struct telos_event *event,
    struct telos_error **error
);

struct telos_state_fragment {
    const char *id;
    enum telos_extension_slot slot;
    const char *const *accepted_event_types;
    size_t accepted_event_type_count;
    telos_state_fragment_handler_fn handle;
    telos_state_fragment_handler_fn compensate;
    uint64_t timeout_milliseconds;
};

bool telos_state_fragment_validate(
    const struct telos_state_fragment *fragment,
    struct telos_error **error
);

bool telos_state_fragment_execute(
    const struct telos_state_fragment *fragment,
    enum telos_extension_slot slot,
    const struct telos_state_fragment_context *context,
    const struct telos_event *event,
    enum telos_fragment_result *result,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
