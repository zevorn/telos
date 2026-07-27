#include <stdio.h>
#include <string.h>

#include <telos/openai_responses.h>

int main(void)
{
    struct telos_value *item = telos_value_new_string("hello");
    const struct telos_value *item_values[] = {item};
    struct telos_value *items = telos_value_new_array(item_values, 1);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *options = telos_value_new_object(NULL, NULL, 0);
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
    bool passed = local_json != NULL
        && remote_json != NULL
        && telos_value_boolean(telos_value_get(local_json, "store"), &store)
        && !store
        && telos_value_get(local_json, "input") == items
        && strcmp(
            telos_value_string(
                telos_value_get(remote_json, "previous_response_id")
            ),
            "resp_previous"
        ) == 0;

    telos_value_release(remote_json);
    telos_value_release(local_json);
    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(item);
    if (!passed) {
        fputs("Provider state modes mapped incorrectly\n", stderr);
        return 1;
    }
    return 0;
}
