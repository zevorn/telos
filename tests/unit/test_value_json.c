#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/value.h>

int main(void)
{
    struct telos_value *status = telos_value_new_string("ready\n\"now\"");
    struct telos_value *count = telos_value_new_integer(42);
    struct telos_value *enabled = telos_value_new_boolean(true);
    const char *keys[] = {"status", "count", "enabled"};
    const struct telos_value *values[] = {status, count, enabled};
    struct telos_value *object = telos_value_new_object(keys, values, 3);
    const size_t json_size = telos_value_json_size(object);
    char *json = malloc(json_size);
    size_t written = 0;
    struct telos_error *error = NULL;
    struct telos_value *parsed;
    int64_t parsed_count = 0;
    bool parsed_enabled = false;

    telos_value_release(enabled);
    telos_value_release(count);
    telos_value_release(status);

    if (json == NULL ||
        !telos_value_write_json(object, json, json_size, &written, &error) ||
        error != NULL || written + 1 != json_size) {
        fputs("failed to serialize Value as JSON\n", stderr);
        telos_error_release(error);
        free(json);
        telos_value_release(object);
        return 1;
    }

    parsed = telos_value_parse_json(json, written, &error);
    if (parsed == NULL || error != NULL ||
        strcmp(telos_value_string(telos_value_get(parsed, "status")),
               "ready\n\"now\"") != 0 ||
        !telos_value_integer(telos_value_get(parsed, "count"), &parsed_count) ||
        parsed_count != 42 ||
        !telos_value_boolean(telos_value_get(parsed, "enabled"),
                             &parsed_enabled) ||
        !parsed_enabled) {
        fputs("JSON round trip changed Value content\n", stderr);
        telos_error_release(error);
        telos_value_release(parsed);
        free(json);
        telos_value_release(object);
        return 1;
    }

    telos_value_release(parsed);
    free(json);
    telos_value_release(object);
    return 0;
}
