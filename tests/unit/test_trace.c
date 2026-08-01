#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <telos/trace.h>

int main(void)
{
    const char *keys[] = {"message", "secret"};
    struct telos_value *message = telos_value_new_string("hello");
    struct telos_value *secret = telos_value_new_sensitive("never-print");
    const struct telos_value *values[] = {message, secret};
    struct telos_value *payload = telos_value_new_object(keys, values, 2);
    struct telos_event_spec spec = {
        .sequence = 7,
        .event_id = {.high = 1, .low = 2},
        .session_id = {.high = 3, .low = 4},
        .correlation_id = {.high = 5, .low = 6},
        .causation_id = {.high = 7, .low = 8},
        .type = "tool.completed",
        .source = "plugin:dev.example.echo",
        .timestamp_milliseconds = 1722168000123,
        .payload = payload,
    };
    struct telos_error *error = NULL;
    struct telos_event *event = telos_event_create(&spec, &error);
    size_t size = telos_event_trace_json_size(event);
    char *json = malloc(size);
    size_t written = 0;

    assert(event != NULL);
    assert(error == NULL);
    assert(json != NULL);
    assert(telos_event_write_trace_json(event, json, size, &written, &error));
    assert(written + 1 == size);
    assert(
        strcmp(
            json,
            "{\"sequence\":7,\"event_id\":\"00000000000000010000000000000002\","
            "\"session_id\":\"00000000000000030000000000000004\","
            "\"correlation_id\":\"00000000000000050000000000000006\","
            "\"causation_id\":\"00000000000000070000000000000008\","
            "\"type\":\"tool.completed\","
            "\"source\":\"plugin:dev.example.echo\","
            "\"timestamp_ms\":1722168000123,"
            "\"payload\":{\"message\":\"hello\",\"secret\":{\"$redacted\":true}"
            "}}") == 0);
    assert(strstr(json, "never-print") == NULL);

    free(json);
    telos_event_release(event);
    telos_value_release(payload);
    telos_value_release(secret);
    telos_value_release(message);
    return 0;
}
