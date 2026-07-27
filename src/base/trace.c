#include <errno.h>
#include <stdlib.h>

#include <telos/trace.h>

static struct telos_value *event_trace_value(const struct telos_event *event)
{
    char event_id[TELOS_ID_TEXT_SIZE];
    char session_id[TELOS_ID_TEXT_SIZE];
    char correlation_id[TELOS_ID_TEXT_SIZE];
    char causation_id[TELOS_ID_TEXT_SIZE];
    const char *keys[] = {
        "sequence",
        "event_id",
        "session_id",
        "correlation_id",
        "causation_id",
        "type",
        "source",
        "timestamp_ms",
        "payload",
    };
    struct telos_value *values[9] = {0};
    struct telos_value *result = NULL;

    if (
        event == NULL
        || !telos_id_format(telos_event_id(event), event_id, sizeof(event_id))
        || !telos_id_format(
            telos_event_session_id(event),
            session_id,
            sizeof(session_id)
        )
        || !telos_id_format(
            telos_event_correlation_id(event),
            correlation_id,
            sizeof(correlation_id)
        )
        || !telos_id_format(
            telos_event_causation_id(event),
            causation_id,
            sizeof(causation_id)
        )
    ) {
        return NULL;
    }

    values[0] = telos_value_new_integer(
        (int64_t)telos_event_sequence(event)
    );
    values[1] = telos_value_new_string(event_id);
    values[2] = telos_value_new_string(session_id);
    values[3] = telos_value_new_string(correlation_id);
    values[4] = telos_value_new_string(causation_id);
    values[5] = telos_value_new_string(telos_event_type(event));
    values[6] = telos_value_new_string(telos_event_source(event));
    values[7] = telos_value_new_integer(
        telos_event_timestamp_milliseconds(event)
    );
    values[8] = telos_value_retain(telos_event_payload(event));

    for (size_t index = 0; index < 9; ++index) {
        if (values[index] == NULL) {
            goto cleanup;
        }
    }
    result = telos_value_new_object(
        keys,
        (const struct telos_value *const *)values,
        9
    );

cleanup:
    for (size_t index = 0; index < 9; ++index) {
        telos_value_release(values[index]);
    }
    return result;
}

size_t telos_event_trace_json_size(const struct telos_event *event)
{
    struct telos_value *trace = event_trace_value(event);
    size_t size = telos_value_json_size(trace);

    telos_value_release(trace);
    return size;
}

bool telos_event_write_trace_json(
    const struct telos_event *event,
    char *buffer,
    size_t buffer_size,
    size_t *written,
    struct telos_error **error
)
{
    struct telos_value *trace;
    bool result;

    if (error != NULL) {
        *error = NULL;
    }
    trace = event_trace_value(event);
    if (trace == NULL) {
        if (error != NULL) {
            *error = telos_error_create(
                event == NULL
                    ? TELOS_ERROR_DOMAIN_ARGUMENT
                    : TELOS_ERROR_DOMAIN_MEMORY,
                event == NULL ? EINVAL : ENOMEM,
                "Event trace could not be constructed",
                NULL
            );
        }
        return false;
    }
    result = telos_value_write_json(
        trace,
        buffer,
        buffer_size,
        written,
        error
    );
    telos_value_release(trace);
    return result;
}
