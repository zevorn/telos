#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/openai_chat.h>

struct fixture {
    size_t events;
    bool saw_secret;
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
    assert(strcmp(reference, "secret:provider.deepseek") == 0);
    assert(strcmp(target, "provider.deepseek") == 0);
    return copy_text("fixture-secret");
}

static bool send_fixture(const struct telos_transport_request *request,
                         telos_transport_chunk_fn receive,
                         void *receive_context,
                         int *status_code,
                         void *transport_context,
                         struct telos_error **error)
{
    static const char first[] =
        "data: {\"id\":\"chatcmpl-fixture\",\"choices\":[{\"index\":0,"
        "\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\n";
    static const char done[] = "data: [DONE]\n\n";
    struct fixture *fixture = transport_context;

    assert(strcmp(request->method, "POST") == 0);
    if (strcmp(request->url,
               "https://fixture.invalid/v1/chat/completions") != 0) {
        fprintf(stderr, "url=%s\n", request->url);
        return false;
    }
    assert(strcmp(request->content_type, "application/json") == 0);
    assert(strcmp(request->accept, "text/event-stream") == 0);
    assert(strcmp(request->bearer_token, "fixture-secret") == 0);
    assert(strstr(request->body, "fixture-model") != NULL);
    assert(strstr(request->body, "fixture-secret") == NULL);
    fixture->saw_secret = true;
    assert(receive(first, sizeof(first) - 1, receive_context, error));
    assert(receive(done, sizeof(done) - 1, receive_context, error));
    *status_code = 200;
    return true;
}

static bool capture_event(const struct telos_provider_event *event,
                          void *context,
                          struct telos_error **error)
{
    struct fixture *fixture = context;

    (void)error;
    fixture->events += 1;
    assert(event != NULL);
    return true;
}

int main(void)
{
    const char *capabilities[] = {
        "network.https",
        "secret.use:provider.deepseek",
    };
    struct fixture fixture = {0};
    struct telos_secret_broker *broker =
        telos_secret_broker_create(resolve, NULL, NULL);
    const struct telos_openai_chat_config config = {
        .model = "fixture-model",
        .endpoint = "https://fixture.invalid/v1",
        .secret_reference = "secret:provider.deepseek",
        .secret_target = "provider.deepseek",
        .secret_broker = broker,
        .capabilities = capabilities,
        .capability_count = 2,
        .send = send_fixture,
        .transport_context = &fixture,
        .unknown_event_policy = TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR,
    };
    struct telos_openai_chat_provider *provider =
        telos_openai_chat_provider_create(&config, NULL);
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
                  telos_openai_chat_provider_dispatch(
                      &request, capture_event, &fixture, provider, &error) &&
                  error == NULL && fixture.saw_secret && fixture.events == 3;

    telos_error_release(error);
    telos_openai_chat_provider_destroy(provider);
    telos_secret_broker_destroy(broker);
    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    if (!passed) {
        fprintf(stderr, "events=%zu secret=%d\n", fixture.events,
                fixture.saw_secret);
        fputs("Chat Provider transport contract failed\n", stderr);
        return 1;
    }
    return 0;
}
