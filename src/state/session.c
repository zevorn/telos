#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <telos/session.h>

struct telos_session_machine {
    enum telos_session_state state;
    enum telos_session_state retry_state;
    uint64_t last_sequence;
    uint64_t pending_tools;
    unsigned int retry_count;
    unsigned int maximum_retry_attempts;
};

struct transition {
    enum telos_session_state from;
    const char *event_type;
    enum telos_session_state to;
};

static const struct transition transitions[] = {
    {
        TELOS_SESSION_IDLE,
        "turn.accepted",
        TELOS_SESSION_TURN_ACCEPTED,
    },
    {
        TELOS_SESSION_TURN_ACCEPTED,
        "input.prepare",
        TELOS_SESSION_INPUT_PREPARE,
    },
    {
        TELOS_SESSION_INPUT_PREPARE,
        "context.build",
        TELOS_SESSION_CONTEXT_BUILD,
    },
    {
        TELOS_SESSION_CONTEXT_BUILD,
        "provider.dispatch",
        TELOS_SESSION_PROVIDER_DISPATCH,
    },
    {
        TELOS_SESSION_PROVIDER_DISPATCH,
        "response.received",
        TELOS_SESSION_RESPONSE_PROCESS,
    },
    {
        TELOS_SESSION_RESPONSE_PROCESS,
        "tool.authorize",
        TELOS_SESSION_TOOL_AUTHORIZE,
    },
    {
        TELOS_SESSION_TOOL_AUTHORIZE,
        "tool.execute",
        TELOS_SESSION_TOOL_EXECUTE,
    },
    {
        TELOS_SESSION_TOOL_EXECUTE,
        "tool.collect",
        TELOS_SESSION_TOOL_COLLECT,
    },
    {
        TELOS_SESSION_TOOL_COLLECT,
        "context.build",
        TELOS_SESSION_CONTEXT_BUILD,
    },
    {
        TELOS_SESSION_RESPONSE_PROCESS,
        "final.commit",
        TELOS_SESSION_FINAL_COMMIT,
    },
    {
        TELOS_SESSION_FINAL_COMMIT,
        "final.committed",
        TELOS_SESSION_COMPLETED,
    },
    {
        TELOS_SESSION_CANCELLING,
        "cancel.completed",
        TELOS_SESSION_CANCELLED,
    },
    {
        TELOS_SESSION_FAILING,
        "failure.completed",
        TELOS_SESSION_FAILED,
    },
};

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

struct telos_session_machine *telos_session_machine_create(
    struct telos_error **error
)
{
    const struct telos_session_options options = {
        .maximum_retry_attempts = 3,
    };

    return telos_session_machine_create_with_options(&options, error);
}

struct telos_session_machine *telos_session_machine_create_with_options(
    const struct telos_session_options *options,
    struct telos_error **error
)
{
    struct telos_session_machine *machine;

    if (error != NULL) {
        *error = NULL;
    }

    if (options == NULL || options->maximum_retry_attempts == 0) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "session retry limit must be greater than zero"
        );
        return NULL;
    }
    machine = calloc(1, sizeof(*machine));
    if (machine == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "session state machine allocation failed"
        );
        return NULL;
    }

    machine->state = TELOS_SESSION_IDLE;
    machine->maximum_retry_attempts = options->maximum_retry_attempts;
    return machine;
}

void telos_session_machine_destroy(struct telos_session_machine *machine)
{
    free(machine);
}

enum telos_session_state telos_session_machine_state(
    const struct telos_session_machine *machine
)
{
    return machine == NULL ? 0 : machine->state;
}

unsigned int telos_session_machine_retry_count(
    const struct telos_session_machine *machine
)
{
    return machine == NULL ? 0 : machine->retry_count;
}

bool telos_session_machine_apply(
    struct telos_session_machine *machine,
    const struct telos_event *event,
    struct telos_error **error
)
{
    const char *event_type;
    uint64_t sequence;

    if (error != NULL) {
        *error = NULL;
    }

    if (machine == NULL || event == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "session transition arguments are invalid"
        );
        return false;
    }

    sequence = telos_event_sequence(event);
    if (sequence <= machine->last_sequence) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EALREADY,
            "session event sequence is stale"
        );
        return false;
    }

    event_type = telos_event_type(event);
    if (
        machine->state == TELOS_SESSION_TOOL_AUTHORIZE
        && strcmp(event_type, "tool.execute") == 0
    ) {
        int64_t tool_count = 0;

        if (
            telos_value_integer(telos_event_payload(event), &tool_count)
            && tool_count > 0
        ) {
            machine->pending_tools = (uint64_t)tool_count;
        } else {
            machine->pending_tools = 0;
        }
        machine->state = TELOS_SESSION_TOOL_EXECUTE;
        machine->last_sequence = sequence;
        return true;
    }

    if (
        machine->state == TELOS_SESSION_TOOL_EXECUTE
        && strcmp(event_type, "tool.completed") == 0
        && machine->pending_tools > 0
    ) {
        machine->pending_tools -= 1;
        if (machine->pending_tools == 0) {
            machine->state = TELOS_SESSION_TOOL_COLLECT;
        }
        machine->last_sequence = sequence;
        return true;
    }

    if (
        machine->state == TELOS_SESSION_TOOL_EXECUTE
        && strcmp(event_type, "tool.collect") == 0
        && machine->pending_tools > 0
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EBUSY,
            "Tool Calls are still running"
        );
        return false;
    }

    if (
        strcmp(event_type, "cancel.requested") == 0
        && machine->state != TELOS_SESSION_IDLE
        && machine->state != TELOS_SESSION_COMPLETED
        && machine->state != TELOS_SESSION_CANCELLING
        && machine->state != TELOS_SESSION_CANCELLED
        && machine->state != TELOS_SESSION_FAILED
    ) {
        machine->state = TELOS_SESSION_CANCELLING;
        machine->last_sequence = sequence;
        return true;
    }

    if (
        strcmp(event_type, "error.retryable") == 0
        && machine->state != TELOS_SESSION_IDLE
        && machine->state != TELOS_SESSION_COMPLETED
        && machine->state != TELOS_SESSION_CANCELLED
        && machine->state != TELOS_SESSION_RETRYING
        && machine->state != TELOS_SESSION_FAILED
    ) {
        if (machine->retry_count >= machine->maximum_retry_attempts) {
            machine->state = TELOS_SESSION_FAILING;
            machine->retry_state = 0;
            machine->last_sequence = sequence;
            return true;
        }
        machine->retry_count += 1;
        machine->retry_state = machine->state;
        machine->state = TELOS_SESSION_RETRYING;
        machine->last_sequence = sequence;
        return true;
    }

    if (
        (
            strcmp(event_type, "error.fatal") == 0
            || strcmp(event_type, "timeout") == 0
        )
        && machine->state != TELOS_SESSION_IDLE
        && machine->state != TELOS_SESSION_COMPLETED
        && machine->state != TELOS_SESSION_CANCELLED
        && machine->state != TELOS_SESSION_FAILED
        && machine->state != TELOS_SESSION_FAILING
    ) {
        machine->state = TELOS_SESSION_FAILING;
        machine->retry_state = 0;
        machine->last_sequence = sequence;
        return true;
    }

    if (
        strcmp(event_type, "retry.resume") == 0
        && machine->state == TELOS_SESSION_RETRYING
        && machine->retry_state != 0
    ) {
        machine->state = machine->retry_state;
        machine->retry_state = 0;
        machine->last_sequence = sequence;
        return true;
    }

    for (size_t index = 0; index < sizeof(transitions) / sizeof(transitions[0]); ++index) {
        if (
            transitions[index].from == machine->state
            && strcmp(transitions[index].event_type, event_type) == 0
        ) {
            machine->state = transitions[index].to;
            machine->last_sequence = sequence;
            return true;
        }
    }

    set_error(
        error,
        TELOS_ERROR_DOMAIN_STATE,
        EINVAL,
        "event is not valid in the current session state"
    );
    return false;
}
