#include <stdio.h>
#include <string.h>

#include <telos/plugins/openai_chat.h>

int main(void)
{
    struct telos_value *content = telos_value_new_string("hello");
    struct telos_value *role = telos_value_new_string("user");
    const char *message_keys[] = {"role", "content"};
    const struct telos_value *message_values[] = {role, content};
    struct telos_value *message =
        telos_value_new_object(message_keys, message_values, 2);
    const struct telos_value *item_values[] = {message};
    struct telos_value *items = telos_value_new_array(item_values, 1);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *temperature = telos_value_new_real(0.25);
    const char *option_keys[] = {"temperature"};
    const struct telos_value *option_values[] = {temperature};
    struct telos_value *options =
        telos_value_new_object(option_keys, option_values, 1);
    const struct telos_provider_request request = {
        .instructions = "be concise",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_value *body =
        telos_openai_chat_build_request("fixture-model", &request, NULL);
    const struct telos_value *messages = telos_value_get(body, "messages");
    const struct telos_value *system = telos_value_at(messages, 0);
    const struct telos_value *user = telos_value_at(messages, 1);
    bool stream = false;
    double mapped_temperature = 0.0;
    bool passed = body != NULL && messages != NULL &&
                  telos_value_count(messages) == 2 &&
                  strcmp(telos_value_string(telos_value_get(system, "role")),
                         "system") == 0 &&
                  strcmp(telos_value_string(
                             telos_value_get(system, "content")),
                         "be concise") == 0 &&
                  user == message &&
                  telos_value_boolean(telos_value_get(body, "stream"),
                                      &stream) &&
                  stream && telos_value_real(telos_value_get(body, "temperature"),
                                             &mapped_temperature) &&
                  mapped_temperature == 0.25 &&
                  telos_value_get(body, "instructions") == NULL &&
                  telos_value_get(body, "input") == NULL;

    {
        struct telos_value *override = telos_value_new_boolean(false);
        const char *keys[] = {"stream"};
        const struct telos_value *values[] = {override};
        struct telos_value *invalid_options =
            telos_value_new_object(keys, values, 1);
        struct telos_provider_request invalid = request;
        struct telos_error *error = NULL;
        struct telos_value *invalid_body;

        invalid.options = invalid_options;
        invalid_body = telos_openai_chat_build_request("fixture-model",
                                                       &invalid, &error);
        passed = passed && invalid_body == NULL && error != NULL;
        telos_value_release(invalid_body);
        telos_error_release(error);
        telos_value_release(invalid_options);
        telos_value_release(override);
    }
    telos_value_release(body);
    telos_value_release(options);
    telos_value_release(temperature);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(message);
    telos_value_release(role);
    telos_value_release(content);
    if (!passed) {
        fputs("Chat Completions request mapping failed\n", stderr);
        return 1;
    }
    return 0;
}
