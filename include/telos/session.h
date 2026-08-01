#ifndef TELOS_SESSION_H
#define TELOS_SESSION_H

#include <telos/types.h>

#include <telos/error.h>
#include <telos/event.h>

enum telos_session_state {
    TELOS_SESSION_IDLE = 1,
    TELOS_SESSION_TURN_ACCEPTED,
    TELOS_SESSION_INPUT_PREPARE,
    TELOS_SESSION_CONTEXT_BUILD,
    TELOS_SESSION_PROVIDER_DISPATCH,
    TELOS_SESSION_RESPONSE_PROCESS,
    TELOS_SESSION_TOOL_AUTHORIZE,
    TELOS_SESSION_TOOL_EXECUTE,
    TELOS_SESSION_TOOL_COLLECT,
    TELOS_SESSION_FINAL_COMMIT,
    TELOS_SESSION_COMPLETED,
    TELOS_SESSION_CANCELLING,
    TELOS_SESSION_CANCELLED,
    TELOS_SESSION_RETRYING,
    TELOS_SESSION_FAILING,
    TELOS_SESSION_FAILED,
};

struct telos_session_machine;
typedef struct telos_session_machine telos_session_machine;

struct telos_session_options {
    unsigned int maximum_retry_attempts;
};

typedef struct telos_session_options telos_session_options;

telos_session_machine *telos_session_machine_create(struct telos_error **error);

telos_session_machine *
telos_session_machine_create_with_options(const telos_session_options *options,
                                          struct telos_error **error);

void telos_session_machine_destroy(telos_session_machine *machine);

enum telos_session_state
telos_session_machine_state(const telos_session_machine *machine);

unsigned int
telos_session_machine_retry_count(const telos_session_machine *machine);

bool telos_session_machine_apply(telos_session_machine *machine,
                                 const struct telos_event *event,
                                 struct telos_error **error);

#endif
