#include <stdio.h>
#include <string.h>

#include <telos/event.h>

int main(void)
{
    char type[] = "tool.completed";
    char source[] = "plugin:echo";
    struct telos_value *payload = telos_value_new_string("result");
    const struct telos_event_spec spec = {
        .sequence = 42,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = type,
        .source = source,
        .timestamp_milliseconds = INT64_C(123456789),
        .payload = payload,
    };
    struct telos_error *error = NULL;
    struct telos_event *event = telos_event_create(&spec, &error);

    type[0] = 'X';
    source[0] = 'X';
    telos_value_release(payload);

    if (event == NULL || error != NULL || telos_event_sequence(event) != 42 ||
        !telos_id_equal(telos_event_id(event), spec.event_id) ||
        !telos_id_equal(telos_event_session_id(event), spec.session_id) ||
        !telos_id_equal(telos_event_correlation_id(event),
                        spec.correlation_id) ||
        !telos_id_equal(telos_event_causation_id(event), spec.causation_id) ||
        strcmp(telos_event_type(event), "tool.completed") != 0 ||
        strcmp(telos_event_source(event), "plugin:echo") != 0 ||
        telos_event_timestamp_milliseconds(event) != INT64_C(123456789) ||
        strcmp(telos_value_string(telos_event_payload(event)), "result") != 0) {
        fputs("event did not preserve immutable metadata\n", stderr);
        telos_error_release(error);
        telos_event_release(event);
        return 1;
    }

    telos_event_release(event);
    return 0;
}
