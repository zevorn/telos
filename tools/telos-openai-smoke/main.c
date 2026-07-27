#include <curl/curl.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/agent.h>
#include <telos/openai_responses.h>

struct receive_context {
    telos_transport_chunk_fn receive;
    void *context;
    struct telos_error **error;
    bool failed;
};

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static char *resolve_secret(
    const char *reference,
    const char *target,
    void *context,
    struct telos_error **error
)
{
    const char *value = getenv("OPENAI_API_KEY");

    (void)context;
    if (
        strcmp(reference, "secret:provider.openai") != 0
        || strcmp(target, "provider.openai") != 0
        || value == NULL
        || value[0] == '\0'
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            ENOENT,
            "OPENAI_API_KEY is not available at the trusted boundary"
        );
        return NULL;
    }
    return copy_text(value);
}

static size_t receive_curl_data(
    char *data,
    size_t size,
    size_t count,
    void *context
)
{
    struct receive_context *receiver = context;
    size_t total;

    if (size != 0 && count > SIZE_MAX / size) {
        receiver->failed = true;
        return 0;
    }
    total = size * count;
    if (
        total > 0
        && !receiver->receive(
            data,
            total,
            receiver->context,
            receiver->error
        )
    ) {
        receiver->failed = true;
        return 0;
    }
    return total;
}

static bool send_https(
    const struct telos_transport_request *request,
    telos_transport_chunk_fn receive,
    void *receive_context,
    int *status_code,
    void *transport_context,
    struct telos_error **error
)
{
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    struct curl_slist *next_header;
    struct receive_context receiver = {
        .receive = receive,
        .context = receive_context,
        .error = error,
    };
    CURLcode result;
    long response_code = 0;
    char curl_error[CURL_ERROR_SIZE] = {0};
    bool success = false;

    (void)transport_context;
    if (
        curl == NULL
        || request == NULL
        || receive == NULL
        || status_code == NULL
    ) {
        curl_easy_cleanup(curl);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "HTTPS Transport arguments are invalid"
        );
        return false;
    }
    next_header = curl_slist_append(
        headers,
        "Content-Type: application/json"
    );
    if (next_header == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "HTTPS header allocation failed"
        );
        goto cleanup;
    }
    headers = next_header;
    next_header = curl_slist_append(headers, "Accept: text/event-stream");
    if (next_header == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "HTTPS header allocation failed"
        );
        goto cleanup;
    }
    headers = next_header;
    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDSIZE_LARGE,
        (curl_off_t)request->body_size
    );
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, request->bearer_token);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_curl_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &receiver);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "telos-openai-smoke/0.1");

    result = curl_easy_perform(curl);
    if (
        result != CURLE_OK
        || receiver.failed
        || curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code)
            != CURLE_OK
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            result == CURLE_OPERATION_TIMEDOUT ? ETIMEDOUT : EIO,
            curl_error[0] == '\0'
                ? "HTTPS Transport failed"
                : curl_error
        );
        goto cleanup;
    }
    *status_code = (int)response_code;
    success = true;

cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}

static enum telos_policy_decision deny_tools(
    const struct telos_policy_request *request,
    void *context
)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_DENY;
}

static void usage(FILE *stream)
{
    fputs(
        "usage: telos-openai-smoke [PROMPT]\n"
        "\n"
        "Required environment:\n"
        "  OPENAI_API_KEY       API key resolved only by the Secret Broker\n"
        "  TELOS_OPENAI_MODEL   Responses-compatible model ID\n"
        "\n"
        "Optional environment:\n"
        "  TELOS_OPENAI_ENDPOINT  defaults to https://api.openai.com/v1\n",
        stream
    );
}

int main(int argc, char **argv)
{
    static const char default_prompt[] =
        "Reply with exactly: Telos remote provider smoke passed.";
    const char *capabilities[] = {
        "network.https",
        "secret.use:provider.openai",
    };
    const char *model = getenv("TELOS_OPENAI_MODEL");
    const char *endpoint = getenv("TELOS_OPENAI_ENDPOINT");
    const char *prompt = argc == 2 ? argv[1] : default_prompt;
    struct telos_secret_broker *secret_broker = NULL;
    struct telos_openai_responses_provider *provider = NULL;
    struct telos_registry *registry = NULL;
    struct telos_registry_generation *generation = NULL;
    struct telos_capability_broker *capability_broker = NULL;
    struct telos_value *role = NULL;
    struct telos_value *content = NULL;
    struct telos_value *item = NULL;
    struct telos_value *items = NULL;
    struct telos_value *tools = NULL;
    struct telos_value *provider_options = NULL;
    struct telos_agent_result agent_result = {0};
    struct telos_error *error = NULL;
    int exit_code = 1;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (argc > 2) {
        usage(stderr);
        return 2;
    }
    if (model == NULL || model[0] == '\0') {
        fputs("telos-openai-smoke: TELOS_OPENAI_MODEL is required\n", stderr);
        return 2;
    }
    if (endpoint == NULL || endpoint[0] == '\0') {
        endpoint = "https://api.openai.com/v1";
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fputs("telos-openai-smoke: libcurl initialization failed\n", stderr);
        return 1;
    }

    secret_broker = telos_secret_broker_create(
        resolve_secret,
        NULL,
        &error
    );
    {
        const struct telos_openai_responses_config config = {
            .model = model,
            .endpoint = endpoint,
            .secret_reference = "secret:provider.openai",
            .secret_broker = secret_broker,
            .capabilities = capabilities,
            .capability_count = 2,
            .send = send_https,
            .unknown_event_policy = TELOS_OPENAI_UNKNOWN_EVENT_IGNORE,
        };

        provider = telos_openai_responses_provider_create(&config, &error);
    }
    registry = telos_registry_create(NULL, 0, &error);
    generation = telos_registry_acquire(registry);
    capability_broker = telos_capability_broker_create(
        NULL,
        0,
        deny_tools,
        NULL,
        &error
    );
    role = telos_value_new_string("user");
    content = telos_value_new_string(prompt);
    {
        const char *keys[] = {"role", "content"};
        const struct telos_value *values[] = {role, content};

        item = telos_value_new_object(keys, values, 2);
    }
    {
        const struct telos_value *values[] = {item};

        items = telos_value_new_array(values, 1);
    }
    tools = telos_value_new_array(NULL, 0);
    provider_options = telos_value_new_object(NULL, NULL, 0);
    if (
        secret_broker == NULL
        || provider == NULL
        || registry == NULL
        || generation == NULL
        || capability_broker == NULL
        || role == NULL
        || content == NULL
        || item == NULL
        || items == NULL
        || tools == NULL
        || provider_options == NULL
    ) {
        goto cleanup;
    }
    {
        const struct telos_provider_request request = {
            .instructions = "Follow the user request. Do not call tools.",
            .items = items,
            .tools = tools,
            .options = provider_options,
            .state_mode = TELOS_PROVIDER_STATE_LOCAL,
        };
        const struct telos_agent_options options = {
            .registry_generation = generation,
            .capability_broker = capability_broker,
            .dispatch = telos_openai_responses_provider_dispatch,
            .provider_context = provider,
            .maximum_provider_rounds = 2,
        };

        if (!telos_agent_run(
            &options,
            &request,
            NULL,
            &agent_result,
            &error
        )) {
            goto cleanup;
        }
    }
    puts(agent_result.text);
    fprintf(
        stderr,
        "Telos remote smoke completed: provider_rounds=%zu tool_calls=%zu\n",
        agent_result.provider_rounds,
        agent_result.tool_calls
    );
    exit_code = 0;

cleanup:
    if (exit_code != 0) {
        fprintf(
            stderr,
            "telos-openai-smoke: %s\n",
            error == NULL
                ? "initialization failed"
                : telos_error_message(error)
        );
    }
    telos_error_release(error);
    telos_agent_result_clear(&agent_result);
    telos_value_release(provider_options);
    telos_value_release(tools);
    telos_value_release(items);
    telos_value_release(item);
    telos_value_release(content);
    telos_value_release(role);
    telos_capability_broker_destroy(capability_broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_openai_responses_provider_destroy(provider);
    telos_secret_broker_destroy(secret_broker);
    curl_global_cleanup();
    return exit_code;
}
