#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/openai_responses.h>

enum send_mode {
    SEND_FAIL,
    SEND_STATUS,
    SEND_PARTIAL,
    SEND_EVENT,
};

struct context {
    enum send_mode mode;
    int status;
    const char *expected_url;
    bool resolver_fails;
};

static char *resolve(const char *reference,
                     const char *target,
                     void *context,
                     struct telos_error **error)
{
    struct context *state = context;
    char *secret;

    (void)reference;
    (void)target;
    if (state->resolver_fails) {
        return NULL;
    }
    secret = malloc(sizeof("secret"));
    if (secret != NULL) {
        memcpy(secret, "secret", sizeof("secret"));
    } else if (error != NULL) {
        *error = telos_error_create(TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                                    "test allocation failed", NULL);
    }
    return secret;
}

static bool send_response(const struct telos_transport_request *request,
                          telos_transport_chunk_fn receive,
                          void *receive_context,
                          int *status_code,
                          void *transport_context,
                          struct telos_error **error)
{
    struct context *context = transport_context;
    static const char partial[] = "event: response.created\n"
                                  "data: {\"type\":\"response.created\"}";
    static const char event[] = "event: response.failed\n"
                                "data: {\"type\":\"response.failed\"}\n\n";

    if (context->expected_url != NULL &&
        strcmp(request->url, context->expected_url) != 0) {
        return false;
    }
    *status_code = context->status;
    if (context->mode == SEND_FAIL) {
        return false;
    }
    if (context->mode == SEND_PARTIAL) {
        return receive(partial, strlen(partial), receive_context, error);
    }
    if (context->mode == SEND_EVENT) {
        return receive(event, strlen(event), receive_context, error);
    }
    return true;
}

static bool accept_event(const struct telos_provider_event *event,
                         void *context,
                         struct telos_error **error)
{
    (void)event;
    (void)context;
    (void)error;
    return true;
}

static bool reject_event(const struct telos_provider_event *event,
                         void *context,
                         struct telos_error **error)
{
    (void)event;
    (void)context;
    (void)error;
    return false;
}

static bool create_rejected(struct telos_openai_responses_config config)
{
    struct telos_error *error = NULL;
    struct telos_openai_responses_provider *provider =
        telos_openai_provider_create(&config, &error);
    bool rejected = provider == NULL && error != NULL;

    telos_openai_provider_destroy(provider);
    telos_error_release(error);
    return rejected;
}

static bool dispatch_rejected(struct telos_openai_responses_provider *provider,
                              const struct telos_provider_request *request,
                              telos_provider_event_fn callback)
{
    struct telos_error *error = NULL;
    bool rejected = !telos_openai_provider_dispatch(request, callback, NULL,
                                                    provider, &error) &&
                    error != NULL;

    telos_error_release(error);
    return rejected;
}

int main(void)
{
    char oversized_header_value[4094];
    const char *capabilities[] = {
        "network.https",
        "secret.use:provider.openai",
    };
    const char *network_only[] = {"network.https"};
    const char *missing_network[] = {"secret.use:provider.openai"};
    const char *null_capability[] = {"network.https", NULL};
    const char *empty_capability[] = {"network.https", ""};
    const struct telos_transport_header valid_header[] = {
        {
            .name = "originator",
            .value = "telos",
        },
    };
    const struct telos_transport_header null_header_name[] = {
        {
            .value = "telos",
        },
    };
    const struct telos_transport_header empty_header_name[] = {
        {
            .name = "",
            .value = "telos",
        },
    };
    const struct telos_transport_header null_header_value[] = {
        {
            .name = "originator",
        },
    };
    const struct telos_transport_header injected_header[] = {
        {
            .name = "originator",
            .value = "telos\r\ninjected: value",
        },
    };
    const struct telos_transport_header oversized_header[] = {
        {
            .name = "originator",
            .value = oversized_header_value,
        },
    };
    struct context context = {
        .mode = SEND_STATUS,
        .status = 200,
        .expected_url = "https://fixture.invalid/v1/responses",
    };
    struct telos_secret_broker *broker =
        telos_secret_broker_create(resolve, &context, NULL);
    struct telos_openai_responses_config config = {
        .model = "fixture",
        .endpoint = "https://fixture.invalid/v1",
        .secret_reference = "secret:provider.openai",
        .secret_broker = broker,
        .capabilities = capabilities,
        .capability_count = 2,
        .send = send_response,
        .transport_context = &context,
        .unknown_event_policy = TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
    };
    struct telos_value *items = telos_value_new_array(NULL, 0);
    struct telos_value *tools = telos_value_new_array(NULL, 0);
    struct telos_value *options = telos_value_new_object(NULL, NULL, 0);
    struct telos_provider_request request = {
        .instructions = "test",
        .items = items,
        .tools = tools,
        .options = options,
        .state_mode = TELOS_PROVIDER_STATE_LOCAL,
    };
    struct telos_openai_responses_config invalid = config;
    struct telos_openai_responses_provider *provider;
    struct telos_error *error = NULL;
    bool passed =
        telos_openai_provider_create(NULL, &error) == NULL && error != NULL;

    telos_error_release(error);
    error = NULL;
    invalid.model = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.model = "";
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.endpoint = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.endpoint = "";
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.secret_reference = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.secret_broker = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.send = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.capabilities = NULL;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.capabilities = missing_network;
    invalid.capability_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.capabilities = null_capability;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.capabilities = empty_capability;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.capabilities = empty_capability;
    invalid.capability_count = SIZE_MAX;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = NULL;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = valid_header;
    invalid.header_count = SIZE_MAX;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = valid_header;
    invalid.header_count = 17;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = null_header_name;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = empty_header_name;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = null_header_value;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.headers = injected_header;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    memset(oversized_header_value, 'a', sizeof(oversized_header_value));
    oversized_header_value[sizeof(oversized_header_value) - 1] = '\0';
    invalid = config;
    invalid.headers = oversized_header;
    invalid.header_count = 1;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.unknown_event_policy = 0;
    passed = passed && create_rejected(invalid);
    invalid = config;
    invalid.secret_reference = "not-a-reference";
    passed = passed && create_rejected(invalid);

    provider = telos_openai_provider_create(&config, NULL);
    passed = passed && provider != NULL &&
             dispatch_rejected(NULL, &request, accept_event) &&
             dispatch_rejected(provider, NULL, accept_event) &&
             dispatch_rejected(provider, &request, NULL);

    request.state_mode = 0;
    passed = passed && dispatch_rejected(provider, &request, accept_event);
    request.state_mode = TELOS_PROVIDER_STATE_LOCAL;

    context.mode = SEND_FAIL;
    passed = passed && dispatch_rejected(provider, &request, accept_event);
    context.mode = SEND_STATUS;
    context.status = 401;
    passed = passed && dispatch_rejected(provider, &request, accept_event);
    context.mode = SEND_PARTIAL;
    context.status = 200;
    passed = passed && dispatch_rejected(provider, &request, accept_event);
    context.mode = SEND_EVENT;
    passed = passed && dispatch_rejected(provider, &request, reject_event);
    telos_openai_provider_destroy(provider);

    invalid = config;
    invalid.endpoint = "https://fixture.invalid/v1/";
    invalid.capabilities = network_only;
    invalid.capability_count = 1;
    context.expected_url = "https://fixture.invalid/v1/responses";
    provider = telos_openai_provider_create(&invalid, NULL);
    context.resolver_fails = true;
    passed = passed && provider != NULL &&
             dispatch_rejected(provider, &request, accept_event);
    context.resolver_fails = false;
    telos_openai_provider_destroy(provider);
    telos_openai_provider_destroy(NULL);

    telos_value_release(options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_secret_broker_destroy(broker);
    if (!passed) {
        fputs("Responses Provider validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
