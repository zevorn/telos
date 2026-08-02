#include <stdio.h>
#include <string.h>

#include <telos/plugins/anthropic.h>

int main(void)
{
    struct telos_value *role = telos_value_new_string("user");
    struct telos_value *content = telos_value_new_string("hello");
    const char *message_keys[] = {"role", "content"};
    const struct telos_value *message_values[] = {role, content};
    struct telos_value *message =
        telos_value_new_object(message_keys, message_values, 2);
    const struct telos_value *item_values[] = {message};
    struct telos_value *items = telos_value_new_array(item_values, 1);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *options = telos_value_new_object(NULL, NULL, 0);
    const struct telos_provider_request request = {
        .instructions = "be concise",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_value *body =
        telos_anthropic_build_request("claude-fixture", &request, NULL);
    int64_t max_tokens = 0;
    const struct telos_value *messages = telos_value_get(body, "messages");
    bool passed = body != NULL &&
                  strcmp(telos_value_string(telos_value_get(body, "model")),
                         "claude-fixture") == 0 &&
                  strcmp(telos_value_string(telos_value_get(body, "system")),
                         "be concise") == 0 &&
                  telos_value_count(messages) == 1 &&
                  telos_value_at(messages, 0) == message &&
                  telos_value_integer(telos_value_get(body, "max_tokens"),
                                      &max_tokens) &&
                  max_tokens == 4096 &&
                  telos_value_get(body, "stream") != NULL;

    telos_value_release(body);
    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(message);
    telos_value_release(content);
    telos_value_release(role);
    if (!passed) {
        fputs("Anthropic request mapping failed\n", stderr);
        return 1;
    }
    return 0;
}
