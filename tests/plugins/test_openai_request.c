#include <stdio.h>
#include <string.h>

#include <telos/plugins/openai_responses.h>

int main(void)
{
    struct telos_value *item = telos_value_new_string("hello");
    const struct telos_value *item_values[] = {item};
    struct telos_value *items = telos_value_new_array(item_values, 1);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *temperature = telos_value_new_real(0.25);
    const char *option_keys[] = {"temperature"};
    const struct telos_value *option_values[] = {temperature};
    struct telos_value *options =
        telos_value_new_object(option_keys, option_values, 1);
    const struct telos_provider_request local = {
        .instructions = "be concise",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    const struct telos_provider_request remote = {
        .instructions = "continue",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_REMOTE,
        .previous_response_id = "resp_previous",
    };
    struct telos_value *local_json =
        telos_openai_responses_build_request("fixture-model", &local, NULL);
    struct telos_value *remote_json =
        telos_openai_responses_build_request("fixture-model", &remote, NULL);
    bool store = true;
    bool stream = false;
    double mapped_temperature = 0.0;
    bool passed =
        local_json != NULL && remote_json != NULL &&
        telos_value_boolean(telos_value_get(local_json, "store"), &store) &&
        !store &&
        telos_value_boolean(telos_value_get(local_json, "stream"), &stream) &&
        stream && telos_value_get(local_json, "options") == NULL &&
        telos_value_real(telos_value_get(local_json, "temperature"),
                         &mapped_temperature) &&
        mapped_temperature == 0.25 &&
        telos_value_get(local_json, "input") == items &&
        strcmp(telos_value_string(
                   telos_value_get(remote_json, "previous_response_id")),
               "resp_previous") == 0;

    {
        struct telos_value *override = telos_value_new_boolean(false);
        const char *keys[] = {"stream"};
        const struct telos_value *values[] = {override};
        struct telos_value *invalid_options =
            telos_value_new_object(keys, values, 1);
        struct telos_provider_request invalid = local;
        struct telos_error *error = NULL;
        struct telos_value *invalid_json;

        invalid.options = invalid_options;
        invalid_json = telos_openai_responses_build_request("fixture-model",
                                                            &invalid, &error);
        passed = passed && invalid_json == NULL && error != NULL;
        telos_value_release(invalid_json);
        telos_error_release(error);
        telos_value_release(invalid_options);
        telos_value_release(override);
    }
    telos_value_release(remote_json);
    telos_value_release(local_json);
    telos_value_release(options);
    telos_value_release(temperature);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(item);
    if (!passed) {
        fputs("Provider state modes mapped incorrectly\n", stderr);
        return 1;
    }
    return 0;
}
