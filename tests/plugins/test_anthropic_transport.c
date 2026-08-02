#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/anthropic.h>

struct fixture {
    size_t events;
};

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);

    assert(copy != NULL);
    memcpy(copy, text, size);
    return copy;
}

static char *resolve(const char *reference,
                     const char *target,
                     void *context,
                     struct telos_error **error)
{
    (void)context;
    (void)error;
    assert(strcmp(reference, "secret:provider.anthropic") == 0);
    assert(strcmp(target, "provider.anthropic") == 0);
    return copy_text("fixture-key");
}

static bool send_fixture(const struct telos_transport_request *request,
                         telos_transport_chunk_fn receive,
                         void *receive_context,
                         int *status_code,
                         void *transport_context,
                         struct telos_error **error)
{
    static const char response[] =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{"
        "\"id\":\"msg_fixture\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"ok\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";
    struct fixture *fixture = transport_context;
    bool saw_key = false;

    assert(strcmp(request->method, "POST") == 0);
    assert(strcmp(request->url, "https://fixture.invalid/v1/messages") == 0);
    assert(strcmp(request->accept, "text/event-stream") == 0);
    assert(request->bearer_token == NULL);
    for (size_t index = 0; index < request->header_count; ++index) {
        if (strcmp(request->headers[index].name, "x-api-key") == 0 &&
            strcmp(request->headers[index].value, "fixture-key") == 0) {
            saw_key = true;
        }
    }
    assert(saw_key);
    assert(receive(response, sizeof(response) - 1, receive_context, error));
    *status_code = 200;
    fixture->events += 1;
    return true;
}

static bool capture(const struct telos_provider_event *event,
                    void *context,
                    struct telos_error **error)
{
    (void)event;
    (void)context;
    (void)error;
    return true;
}

int main(void)
{
    const char *capabilities[] = {
        "network.https",
        "secret.use:provider.anthropic",
    };
    const struct telos_transport_header headers[] = {
        {.name = "anthropic-version", .value = "2023-06-01"},
    };
    struct fixture fixture = {0};
    struct telos_secret_broker *broker =
        telos_secret_broker_create(resolve, NULL, NULL);
    const struct telos_anthropic_config config = {
        .model = "claude-fixture",
        .endpoint = "https://fixture.invalid/v1",
        .secret_reference = "secret:provider.anthropic",
        .secret_target = "provider.anthropic",
        .secret_broker = broker,
        .capabilities = capabilities,
        .capability_count = 2,
        .headers = headers,
        .header_count = 1,
        .send = send_fixture,
        .transport_context = &fixture,
        .unknown_event_policy = TELOS_ANTHROPIC_UNKNOWN_EVENT_ERROR,
    };
    struct telos_anthropic_provider *provider =
        telos_anthropic_provider_create(&config, NULL);
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *options = telos_value_new_object(NULL, NULL, 0);
    const struct telos_provider_request request = {
        .instructions = "fixture",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_error *error = NULL;
    bool passed = broker != NULL && provider != NULL &&
                  telos_anthropic_provider_dispatch(
                      &request, capture, &fixture, provider, &error) &&
                  error == NULL && fixture.events == 1;

    telos_error_release(error);
    telos_anthropic_provider_destroy(provider);
    telos_secret_broker_destroy(broker);
    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    if (!passed) {
        fputs("Anthropic transport contract failed\n", stderr);
        return 1;
    }
    return 0;
}
