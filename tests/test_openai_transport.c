#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/openai_responses.h>

struct fixture {
    const char *path;
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

static char *resolve(
    const char *reference,
    const char *target,
    void *context,
    struct telos_error **error
)
{
    (void)context;
    (void)error;
    assert(strcmp(reference, "secret:provider.openai") == 0);
    assert(strcmp(target, "provider.openai") == 0);
    return copy_text("fixture-secret");
}

static bool send_fixture(
    const struct telos_transport_request *request,
    telos_transport_chunk_fn receive,
    void *receive_context,
    int *status_code,
    void *transport_context,
    struct telos_error **error
)
{
    struct fixture *fixture = transport_context;
    FILE *stream = fopen(fixture->path, "rb");
    char chunk[11];
    size_t received;

    assert(stream != NULL);
    assert(strcmp(request->method, "POST") == 0);
    assert(strcmp(request->url, "https://fixture.invalid/v1/responses") == 0);
    assert(strcmp(request->content_type, "application/json") == 0);
    assert(strcmp(request->bearer_token, "fixture-secret") == 0);
    assert(strstr(request->body, "fixture-secret") == NULL);
    fixture->saw_secret = true;
    while ((received = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
        if (!receive(chunk, received, receive_context, error)) {
            fclose(stream);
            return false;
        }
    }
    assert(!ferror(stream));
    fclose(stream);
    *status_code = 200;
    return true;
}

static bool capture(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
)
{
    struct fixture *fixture = context;

    (void)event;
    (void)error;
    fixture->events += 1;
    return true;
}

int main(int argc, char **argv)
{
    const char *capabilities[] = {
        "network.https",
        "secret.use:provider.openai",
    };
    struct fixture fixture;
    struct telos_secret_broker *broker;
    struct telos_openai_responses_config config;
    struct telos_openai_responses_provider *provider;
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *options = telos_value_new_object(NULL, NULL, 0);
    struct telos_provider_request request = {
        .instructions = "fixture",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_error *error = NULL;

    assert(argc == 2);
    fixture = (struct fixture) {.path = argv[1]};
    broker = telos_secret_broker_create(resolve, NULL, &error);
    config = (struct telos_openai_responses_config) {
        .model = "fixture-model",
        .endpoint = "https://fixture.invalid/v1",
        .secret_reference = "secret:provider.openai",
        .secret_broker = broker,
        .capabilities = capabilities,
        .capability_count = 2,
        .send = send_fixture,
        .transport_context = &fixture,
        .unknown_event_policy = TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
    };
    provider = telos_openai_responses_provider_create(&config, &error);
    assert(provider != NULL);
    assert(telos_openai_responses_provider_dispatch(
        &request,
        capture,
        &fixture,
        provider,
        &error
    ));
    assert(error == NULL);
    assert(fixture.saw_secret);
    assert(fixture.events == 10);

    telos_openai_responses_provider_destroy(provider);
    telos_secret_broker_destroy(broker);
    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    return 0;
}
