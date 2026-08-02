#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/agent.h>
#include <telos/authentication.h>
#include <telos/command.h>
#include <telos/model.h>
#include <telos/plugins/curl_transport.h>
#include <telos/plugins/model_catalog.h>
#include <telos/plugins/openai_codex_auth.h>
#include <telos/plugins/openai_chat.h>
#include <telos/plugins/openai_responses.h>
#include <telos/plugins/project_guidance.h>
#include <telos/plugins/terminal_frontend.h>
#include <telos/prompt.h>
#include <telos/registry.h>
#include <telos/secret.h>
#include <telos/tool.h>
#include <telos/value.h>

#include "chat.h"

#define CHAT_PATH_SIZE 4096U
#define CHAT_MAXIMUM_MESSAGES 64U
#define CHAT_MAXIMUM_CONVERSATION_BYTES (4U * 1024U * 1024U)
#define CHAT_MAXIMUM_RESPONSE_BYTES (1024U * 1024U)

struct chat_session {
    const struct telos_authentication_definition_v1 *authentication_definition;
    struct telos_authentication *authentication;
    struct telos_secret_broker *secret_broker;
    void *provider_context;
    telos_provider_dispatch_fn provider_dispatch;
    void (*provider_destroy)(void *provider);
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_capability_broker *capability_broker;
    struct telos_prompt_snapshot *prompt;
    struct telos_value *tools;
    struct telos_value *provider_options;
    struct telos_value *messages[CHAT_MAXIMUM_MESSAGES];
    size_t message_sizes[CHAT_MAXIMUM_MESSAGES];
    size_t message_count;
    size_t conversation_bytes;
    bool loopback_endpoint;
    bool loopback_authentication;
    const char *model;
    const char *configured_endpoint;
    struct telos_transport_header authentication_headers[4];
    struct telos_curl_transport_config transport;
    struct telos_model_catalog model_catalog;
    struct telos_command_registry commands;
    const struct telos_model_descriptor *selected_model;
    const char *configured_provider;
};

struct observer_context {
    telos_frontend_emit_fn emit;
    void *emit_context;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static const char *chat_provider_get(void *context)
{
    const struct chat_session *chat = context;

    if (chat->selected_model != NULL) {
        return chat->selected_model->provider;
    }
    return chat->configured_provider == NULL ? "unconfigured"
                                             : chat->configured_provider;
}

static const char *chat_model_get(void *context)
{
    const struct chat_session *chat = context;

    return chat->selected_model == NULL ? "not configured"
                                        : chat->selected_model->id;
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

static bool host_matches(const char *host, const char *expected)
{
    size_t size = strlen(expected);

    return strncmp(host, expected, size) == 0 &&
           (host[size] == '\0' || host[size] == '/' || host[size] == ':');
}

static bool endpoint_is_loopback(const char *endpoint)
{
    const char *host;

    if (strncmp(endpoint, "http://", 7) == 0) {
        host = endpoint + 7;
    } else if (strncmp(endpoint, "https://", 8) == 0) {
        host = endpoint + 8;
    } else {
        return false;
    }
    return host_matches(host, "localhost") ||
           host_matches(host, "127.0.0.1") || host_matches(host, "[::1]");
}

static bool endpoint_is_allowed(const char *endpoint)
{
    return endpoint != NULL &&
           (strncmp(endpoint, "https://", 8) == 0 ||
            (strncmp(endpoint, "http://", 7) == 0 &&
             endpoint_is_loopback(endpoint)));
}

static char *resolve_secret(const char *reference, const char *target,
                            void *context, struct telos_error **error)
{
    struct chat_session *chat = context;
    const char *value = getenv("TELOS_AGENT_API_KEY");
    const char *provider;

    if (reference == NULL || target == NULL ||
        strncmp(target, "provider.", sizeof("provider.") - 1) != 0 ||
        strncmp(reference, "secret:", sizeof("secret:") - 1) != 0 ||
        strcmp(reference + sizeof("secret:") - 1, target) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Agent secret Reference is not authorized");
        return NULL;
    }
    provider = target + sizeof("provider.") - 1;
    if (strcmp(provider, "openai") == 0 &&
        chat->authentication_definition != NULL &&
        (!chat->loopback_endpoint || chat->loopback_authentication)) {
        struct telos_authentication_status status;

        if (chat->authentication_definition->status(chat->authentication,
                                                     &status, error) &&
            status.state == TELOS_AUTHENTICATION_SIGNED_IN) {
            return chat->authentication_definition->resolve(
                chat->authentication, target, error);
        }
        if (error != NULL && *error != NULL) {
            return NULL;
        }
    }
    if (strcmp(provider, "openai") == 0) {
        if (value == NULL || value[0] == '\0') {
            value = getenv("OPENAI_API_KEY");
        }
    } else if (strcmp(provider, "deepseek") == 0) {
        value = getenv("DEEPSEEK_API_KEY");
    } else if (strcmp(provider, "zai") == 0) {
        value = getenv("ZAI_API_KEY");
        if (value == NULL || value[0] == '\0') {
            value = getenv("Z_AI_API_KEY");
        }
    }
    if ((value == NULL || value[0] == '\0') && chat->loopback_endpoint) {
        value = "local";
    }
    if (value == NULL || value[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_IO, ENOENT,
                  "Provider API credentials are required");
        return NULL;
    }
    return copy_text(value);
}

static bool
authentication_signed_in(const struct chat_session *chat,
                         struct telos_authentication_status *status,
                         struct telos_error **error)
{
    if (chat->authentication_definition == NULL ||
        chat->authentication == NULL) {
        return false;
    }
    if (!chat->authentication_definition->status(chat->authentication,
                                                  status, error)) {
        return false;
    }
    return status->state == TELOS_AUTHENTICATION_SIGNED_IN;
}

static bool create_provider(struct chat_session *chat,
                            struct telos_error **error)
{
    const struct telos_model_descriptor *model = chat->selected_model;
    const char *provider_name =
        model == NULL ? chat->configured_provider : model->provider;
    const char *secret_provider;
    const char *endpoint = chat->configured_endpoint;
    const char *capabilities[] = {
        "network.https",
        NULL,
    };
    char secret_capability[96];
    char secret_reference[96];
    struct telos_authentication_status status;
    void *provider_context = NULL;
    telos_provider_dispatch_fn provider_dispatch = NULL;
    void (*provider_destroy)(void *provider) = NULL;
    const struct telos_transport_header *headers = NULL;
    size_t header_count = 0;
    int written;

    if (model == NULL || provider_name == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent model selection is incomplete");
        return false;
    }
    if (strcmp(provider_name, "openai") == 0) {
        secret_provider = "openai";
    } else if (strcmp(provider_name, "deepseek") == 0) {
        secret_provider = "deepseek";
        if (strcmp(endpoint, "https://api.openai.com/v1") == 0) {
            endpoint = "https://api.deepseek.com";
        }
    } else if (strcmp(provider_name, "zai") == 0) {
        secret_provider = "zai";
        if (strcmp(endpoint, "https://api.openai.com/v1") == 0) {
            endpoint = "https://api.z.ai/api/paas/v4";
        }
    } else {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "The selected Provider Plugin is not available");
        return false;
    }
    written = snprintf(secret_capability, sizeof(secret_capability),
                       "secret.use:provider.%s", secret_provider);
    if (written < 0 || (size_t)written >= sizeof(secret_capability) ||
        snprintf(secret_reference, sizeof(secret_reference), "secret:%s",
                 secret_capability + sizeof("secret.use:") - 1) < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Provider secret target is too long");
        return false;
    }
    capabilities[1] = secret_capability;

    if (model->api == TELOS_MODEL_API_OPENAI_RESPONSES &&
        (!chat->loopback_endpoint || chat->loopback_authentication) &&
        authentication_signed_in(chat, &status, error)) {
        chat->authentication_headers[0] =
            (struct telos_transport_header){
                .name = "chatgpt-account-id",
                .value = status.account_id,
            };
        chat->authentication_headers[1] =
            (struct telos_transport_header){
                .name = "originator",
                .value = "telos",
            };
        chat->authentication_headers[2] =
            (struct telos_transport_header){
                .name = "OpenAI-Beta",
                .value = "responses=experimental",
            };
        chat->authentication_headers[3] =
            (struct telos_transport_header){
                .name = "User-Agent",
                .value = "telos/0.1.0",
            };
        headers = chat->authentication_headers;
        header_count = 4;
        if (!chat->loopback_endpoint) {
            endpoint = TELOS_OPENAI_CODEX_RESPONSES_ENDPOINT;
        }
    } else if (model->api == TELOS_MODEL_API_OPENAI_RESPONSES &&
               (!chat->loopback_endpoint || chat->loopback_authentication) &&
               error != NULL && *error != NULL) {
        return false;
    }
    if (model->api == TELOS_MODEL_API_OPENAI_RESPONSES) {
        const struct telos_openai_responses_config config = {
            .model = chat->model,
            .endpoint = endpoint,
            .secret_reference = secret_reference,
            .secret_broker = chat->secret_broker,
            .capabilities = capabilities,
            .capability_count = 2,
            .headers = headers,
            .header_count = header_count,
            .send = telos_curl_transport_send,
            .transport_context = &chat->transport,
            .unknown_event_policy = TELOS_OPENAI_UNKNOWN_EVENT_IGNORE,
        };

        provider_context = telos_openai_provider_create(&config, error);
        provider_dispatch = telos_openai_provider_dispatch;
        provider_destroy = (void (*)(void *))telos_openai_provider_destroy;
    } else if (model->api == TELOS_MODEL_API_OPENAI_CHAT) {
        const struct telos_openai_chat_config config = {
            .model = chat->model,
            .endpoint = endpoint,
            .secret_reference = secret_reference,
            .secret_target = secret_reference + sizeof("secret:") - 1,
            .secret_broker = chat->secret_broker,
            .capabilities = capabilities,
            .capability_count = 2,
            .headers = headers,
            .header_count = header_count,
            .send = telos_curl_transport_send,
            .transport_context = &chat->transport,
            .unknown_event_policy = TELOS_OPENAI_CHAT_UNKNOWN_EVENT_IGNORE,
        };

        provider_context = telos_openai_chat_provider_create(&config, error);
        provider_dispatch = telos_openai_chat_provider_dispatch;
        provider_destroy =
            (void (*)(void *))telos_openai_chat_provider_destroy;
    } else {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "The selected Provider Plugin API is not available");
        return false;
    }
    if (provider_context == NULL) {
        return false;
    }
    if (chat->provider_destroy != NULL) {
        chat->provider_destroy(chat->provider_context);
    }
    chat->provider_context = provider_context;
    chat->provider_dispatch = provider_dispatch;
    chat->provider_destroy = provider_destroy;
    return true;
}

static enum telos_policy_decision
deny_tools(const struct telos_policy_request *request, void *context)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_DENY;
}

static bool marker_exists(const char *directory, const char *name)
{
    char path[CHAT_PATH_SIZE];
    struct stat status;

    return snprintf(path, sizeof(path), "%s/%s", directory, name) <
               (int)sizeof(path) &&
           stat(path, &status) == 0;
}

static bool find_project_root(const char *current_directory,
                              char root[CHAT_PATH_SIZE])
{
    if (realpath(current_directory, root) == NULL) {
        return false;
    }
    while (!marker_exists(root, ".git") &&
           !marker_exists(root, "telos.toml")) {
        char *separator;

        if (strcmp(root, "/") == 0) {
            return realpath(current_directory, root) != NULL;
        }
        separator = strrchr(root, '/');
        if (separator == root) {
            root[1] = '\0';
        } else {
            *separator = '\0';
        }
    }
    return true;
}

static bool create_prompt(struct chat_session *chat,
                          const char *home_directory,
                          const char *current_directory,
                          struct telos_error **error)
{
    static const char agent_definition[] =
        "You are Telos, an expert coding agent running in the user's terminal. "
        "Help with software engineering tasks, follow the supplied project "
        "guidance, inspect relevant context before acting, and continue until "
        "the request is genuinely handled. Use only tools exposed by Telos, "
        "preserve existing work, keep changes focused, explain important "
        "tradeoffs plainly, and verify results before claiming success.";
    struct telos_prompt_fragment fragments[3];
    char telos_home[CHAT_PATH_SIZE];
    char project_root[CHAT_PATH_SIZE];
    char *user_guidance = NULL;
    char *project_guidance = NULL;
    size_t count = 0;
    bool result = false;

    if (snprintf(telos_home, sizeof(telos_home), "%s/.telos",
                 home_directory) >= (int)sizeof(telos_home) ||
        !find_project_root(current_directory, project_root)) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Agent project root could not be resolved");
        return false;
    }
    if (!telos_guidance_discover(telos_home, project_root, current_directory,
                                 &user_guidance, &project_guidance, error)) {
        return false;
    }
    fragments[count++] = (struct telos_prompt_fragment){
        .slot = TELOS_PROMPT_AGENT_DEFINITION,
        .trust = TELOS_PROMPT_TRUST_CORE,
        .priority = 0,
        .byte_budget = sizeof(agent_definition) - 1,
        .source = "Telos terminal agent",
        .content = agent_definition,
    };
    if (user_guidance[0] != '\0') {
        fragments[count++] = (struct telos_prompt_fragment){
            .slot = TELOS_PROMPT_USER_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_USER,
            .priority = 0,
            .byte_budget = strlen(user_guidance),
            .source = "~/.telos/AGENTS.md",
            .content = user_guidance,
        };
    }
    if (project_guidance[0] != '\0') {
        fragments[count++] = (struct telos_prompt_fragment){
            .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .priority = 0,
            .byte_budget = strlen(project_guidance),
            .source = "project AGENTS.md",
            .content = project_guidance,
        };
    }
    chat->prompt = telos_prompt_snapshot_create(fragments, count, error);
    result = chat->prompt != NULL;
    telos_prompt_string_free(project_guidance);
    telos_prompt_string_free(user_guidance);
    return result;
}

static struct telos_value *create_message(const char *role, const char *text,
                                          struct telos_error **error)
{
    struct telos_value *role_value = telos_value_new_string(role);
    struct telos_value *content_value = telos_value_new_string(text);
    const char *keys[] = {"role", "content"};
    const struct telos_value *values[] = {role_value, content_value};
    struct telos_value *message = NULL;

    if (role_value != NULL && content_value != NULL) {
        message = telos_value_new_object(keys, values, 2);
    }
    telos_value_release(content_value);
    telos_value_release(role_value);
    if (message == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent conversation message allocation failed");
    }
    return message;
}

static void drop_messages(struct chat_session *chat, size_t count)
{
    if (count > chat->message_count) {
        count = chat->message_count;
    }
    for (size_t index = 0; index < count; ++index) {
        chat->conversation_bytes -= chat->message_sizes[index];
        telos_value_release(chat->messages[index]);
    }
    memmove(chat->messages, chat->messages + count,
            (chat->message_count - count) * sizeof(chat->messages[0]));
    memmove(chat->message_sizes, chat->message_sizes + count,
            (chat->message_count - count) * sizeof(chat->message_sizes[0]));
    chat->message_count -= count;
}

static void clear_messages(struct chat_session *chat)
{
    drop_messages(chat, chat->message_count);
}

static bool append_message(struct chat_session *chat, const char *role,
                           const char *text, struct telos_error **error)
{
    size_t size = strlen(text);
    struct telos_value *message;

    if (size > CHAT_MAXIMUM_CONVERSATION_BYTES) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "Agent conversation message is too large");
        return false;
    }
    message = create_message(role, text, error);
    if (message == NULL) {
        return false;
    }
    while (chat->message_count >= CHAT_MAXIMUM_MESSAGES ||
           size > CHAT_MAXIMUM_CONVERSATION_BYTES -
                      chat->conversation_bytes) {
        drop_messages(chat, chat->message_count >= 2 ? 2 : 1);
    }
    chat->messages[chat->message_count] = message;
    chat->message_sizes[chat->message_count] = size;
    chat->message_count += 1;
    chat->conversation_bytes += size;
    return true;
}

static bool emit_frontend(struct observer_context *observer,
                          enum telos_frontend_event_kind kind,
                          const char *text,
                          const char *name,
                          struct telos_error **error)
{
    const struct telos_frontend_event event = {
        .kind = kind,
        .text = text,
        .name = name,
    };

    return observer->emit(&event, observer->emit_context, error);
}

static bool observe_agent(const struct telos_agent_event *event,
                          void *context, struct telos_error **error)
{
    struct observer_context *observer = context;

    if (event->kind == TELOS_AGENT_PROVIDER_EVENT) {
        const struct telos_provider_event *provider = event->provider_event;

        if (provider->kind == TELOS_PROVIDER_RESPONSE_STARTED) {
            return emit_frontend(observer, TELOS_FRONTEND_RESPONSE_STARTED,
                                 NULL, NULL, error);
        }
        if (provider->kind == TELOS_PROVIDER_TEXT_DELTA) {
            return emit_frontend(observer, TELOS_FRONTEND_TEXT_DELTA,
                                 provider->delta, NULL, error);
        }
        return true;
    }
    if (event->kind == TELOS_AGENT_TOOL_STARTED) {
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_STARTED, NULL,
                             event->tool_name, error);
    }
    if (event->kind == TELOS_AGENT_TOOL_COMPLETED) {
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_COMPLETED, NULL,
                             event->tool_name, error);
    }
    if (event->kind == TELOS_AGENT_TOOL_FAILED) {
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_FAILED, NULL,
                             event->tool_name, error);
    }
    return true;
}

static bool
observe_authentication(const struct telos_authentication_event *event,
                       void *context, struct telos_error **error)
{
    struct observer_context *observer = context;
    char message[1024];

    if (event->kind == TELOS_AUTHENTICATION_VERIFICATION_REQUIRED) {
        if (event->verification_uri == NULL || event->user_code == NULL ||
            snprintf(message, sizeof(message),
                     "Open %s and enter code %s",
                     event->verification_uri, event->user_code) >=
                (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "OpenAI verification instructions are invalid");
            return false;
        }
        return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                             error);
    }
    if (event->kind == TELOS_AUTHENTICATION_COMPLETED) {
        return emit_frontend(observer, TELOS_FRONTEND_NOTICE,
                             "OpenAI login completed", NULL, error);
    }
    set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
              "OpenAI authentication Event is invalid");
    return false;
}

static bool login_command(struct chat_session *chat,
                          const struct telos_cancel *cancel,
                          struct observer_context *observer,
                          struct telos_error **error)
{
    if (chat->authentication_definition == NULL ||
        chat->authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "This Provider uses API-key authentication");
        return false;
    }
    if (!chat->authentication_definition->login(
            chat->authentication, cancel, observe_authentication, observer,
            error)) {
        return false;
    }
    return chat->model == NULL || create_provider(chat, error);
}

static bool logout_command(struct chat_session *chat,
                           struct observer_context *observer,
                           struct telos_error **error)
{
    if (chat->authentication_definition == NULL ||
        chat->authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "This Provider uses API-key authentication");
        return false;
    }
    if (!chat->authentication_definition->logout(chat->authentication,
                                                  error)) {
        return false;
    }
    if (chat->model != NULL && !create_provider(chat, error)) {
        return false;
    }
    return emit_frontend(observer, TELOS_FRONTEND_NOTICE,
                         "OpenAI logout completed", NULL, error);
}

static bool login_status_command(const struct chat_session *chat,
                                 struct observer_context *observer,
                                 struct telos_error **error)
{
    struct telos_authentication_status status;
    char message[512];

    if (chat->authentication_definition == NULL ||
        chat->authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "This Provider uses API-key authentication");
        return false;
    }

    if (!chat->authentication_definition->status(chat->authentication,
                                                  &status, error)) {
        return false;
    }
    if (status.state == TELOS_AUTHENTICATION_SIGNED_IN) {
        if (snprintf(message, sizeof(message),
                     "OpenAI is logged in as account %s",
                     status.account_id) >= (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                      "OpenAI account identifier is too long");
            return false;
        }
    } else if (status.state == TELOS_AUTHENTICATION_AUTHORIZING) {
        memcpy(message, "OpenAI login is in progress",
               sizeof("OpenAI login is in progress"));
    } else {
        memcpy(message, "OpenAI is logged out",
               sizeof("OpenAI is logged out"));
    }
    return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                         error);
}

struct model_list_context {
    char text[TELOS_COMMAND_ARGUMENT_SIZE];
    size_t used;
};

static bool append_model_list(const struct telos_model_descriptor *model,
                              void *context, struct telos_error **error)
{
    struct model_list_context *list = context;
    int written;

    (void)error;
    written = snprintf(list->text + list->used,
                       sizeof(list->text) - list->used, "%s/%s\n",
                       model->provider, model->id);
    if (written < 0 || (size_t)written >= sizeof(list->text) - list->used) {
        return false;
    }
    list->used += (size_t)written;
    return true;
}

static bool clear_command(const char *arguments,
                          const struct telos_cancel *cancel,
                          telos_frontend_emit_fn emit, void *emit_context,
                          void *context, struct telos_error **error)
{
    struct chat_session *chat = context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };

    (void)arguments;
    (void)cancel;
    clear_messages(chat);
    return emit_frontend(&observer, TELOS_FRONTEND_NOTICE,
                         "conversation cleared", NULL, error);
}

static bool model_command(const char *arguments,
                          const struct telos_cancel *cancel,
                          telos_frontend_emit_fn emit, void *emit_context,
                          void *context, struct telos_error **error)
{
    struct chat_session *chat = context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };
    struct model_list_context list = {0};
    char message[TELOS_COMMAND_ARGUMENT_SIZE];
    const struct telos_model_descriptor *model;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        if (!telos_model_catalog_visit(&chat->model_catalog,
                                       append_model_list, &list, error)) {
            return false;
        }
        if (list.used == 0) {
            memcpy(list.text, "No models are configured", sizeof(
                       "No models are configured"));
        }
        return emit_frontend(&observer, TELOS_FRONTEND_NOTICE, list.text,
                             NULL, error);
    }
    if (!telos_model_catalog_select_spec(&chat->model_catalog, arguments,
                                         error)) {
        return false;
    }
    model = telos_model_catalog_current(&chat->model_catalog);
    if (model == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                  "Selected model is unavailable");
        return false;
    }
    chat->selected_model = model;
    chat->model = model->id;
    if (!create_provider(chat, error)) {
        return false;
    }
    if (snprintf(message, sizeof(message), "Model set to %s/%s",
                 model->provider, model->id) >= (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Selected model name is too long");
        return false;
    }
    return emit_frontend(&observer, TELOS_FRONTEND_NOTICE, message, NULL,
                         error);
}

static bool login_command_handler(const char *arguments,
                                  const struct telos_cancel *cancel,
                                  telos_frontend_emit_fn emit,
                                  void *emit_context, void *context,
                                  struct telos_error **error)
{
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };

    if (arguments != NULL && strcmp(arguments, "status") == 0) {
        return login_status_command(context, &observer, error);
    }
    return login_command(context, cancel, &observer, error);
}

static bool logout_command_handler(const char *arguments,
                                   const struct telos_cancel *cancel,
                                   telos_frontend_emit_fn emit,
                                   void *emit_context, void *context,
                                   struct telos_error **error)
{
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };

    (void)arguments;
    (void)cancel;
    return logout_command(context, &observer, error);
}

static bool login_status_command_handler(const char *arguments,
                                         const struct telos_cancel *cancel,
                                         telos_frontend_emit_fn emit,
                                         void *emit_context, void *context,
                                         struct telos_error **error)
{
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };

    (void)arguments;
    (void)cancel;
    return login_status_command(context, &observer, error);
}

static bool chat_turn(const char *input, const struct telos_cancel *cancel,
                      telos_frontend_emit_fn emit, void *emit_context,
                      void *turn_context, struct telos_error **error)
{
    struct chat_session *chat = turn_context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
    };
    struct telos_agent_result result = {0};
    struct telos_value *items = NULL;
    bool success = false;

    if (chat->provider_context == NULL || chat->provider_dispatch == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Set agent.model or TELOS_AGENT_MODEL before sending "
                  "a prompt");
        return false;
    }
    if (!append_message(chat, "user", input, error)) {
        return false;
    }
    items = telos_value_new_array(
        (const struct telos_value *const *)chat->messages,
        chat->message_count);
    if (items == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent conversation Snapshot allocation failed");
        goto cleanup;
    }
    {
        const struct telos_provider_request request = {
            .instructions = telos_prompt_snapshot_content(chat->prompt),
            .items = items,
            .tools = chat->tools,
            .options = chat->provider_options,
            .state_mode = TELOS_PROVIDER_STATE_LOCAL,
        };
        const struct telos_agent_options options = {
            .registry_generation = chat->generation,
            .capability_broker = chat->capability_broker,
            .dispatch = chat->provider_dispatch,
            .provider_context = chat->provider_context,
            .observe = observe_agent,
            .observe_context = &observer,
            .maximum_provider_rounds = 8,
            .maximum_response_bytes = CHAT_MAXIMUM_RESPONSE_BYTES,
        };

        if (!telos_agent_run(&options, &request, cancel, &result, error)) {
            goto cleanup;
        }
    }
    success = append_message(chat, "assistant", result.text, error);

cleanup:
    telos_agent_result_clear(&result);
    telos_value_release(items);
    return success;
}

static bool register_chat_commands(struct chat_session *chat,
                                   struct telos_error **error)
{
    const struct telos_command commands[] = {
        {
            .name = "clear",
            .help = "clear the Agent conversation",
            .run = clear_command,
            .context = chat,
        },
        {
            .name = "model",
            .help = "list or select a model",
            .run = model_command,
            .context = chat,
        },
        {
            .name = "login",
            .help = "sign in to a Provider",
            .run = login_command_handler,
            .context = chat,
        },
        {
            .name = "logout",
            .help = "sign out of a Provider",
            .run = logout_command_handler,
            .context = chat,
        },
        {
            .name = "login-status",
            .help = "show Provider authentication status",
            .run = login_status_command_handler,
            .context = chat,
        },
    };

    telos_command_registry_initialize(&chat->commands);
    for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]);
         ++index) {
        if (!telos_command_registry_add(&chat->commands, &commands[index],
                                        error)) {
            return false;
        }
    }
    return true;
}

static bool select_configured_model(struct chat_session *chat,
                                    const char *provider_name,
                                    const char *model,
                                    struct telos_error **error)
{
    const char *provider =
        strcmp(provider_name, "openai") == 0 ||
        strcmp(provider_name, "openai-responses") == 0 ||
        strcmp(provider_name, "dev.zevorn.openai-responses") == 0
                               ? "openai"
                               : (strcmp(provider_name, "openai-chat") == 0 ||
                                          strcmp(provider_name,
                                                 "dev.zevorn.openai-chat") == 0
                                      ? "openai"
                                      : provider_name);

    chat->configured_provider = provider;
    if (model == NULL || model[0] == '\0' || strcmp(model, "unconfigured") ==
                                               0) {
        return true;
    }
    if (telos_model_catalog_find(&chat->model_catalog, provider, model) ==
        NULL) {
        const struct telos_model_descriptor custom = {
            .provider = provider,
            .id = model,
            .name = model,
            .api = strcmp(provider, "openai") == 0
                       ? TELOS_MODEL_API_OPENAI_RESPONSES
                       : TELOS_MODEL_API_OPENAI_CHAT,
            .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                            TELOS_MODEL_CAPABILITY_TOOLS,
        };

        if (!telos_model_catalog_add(&chat->model_catalog, &custom, error)) {
            return false;
        }
    }
    if (!telos_model_catalog_select(&chat->model_catalog, provider, model,
                                    error)) {
        return false;
    }
    chat->selected_model = telos_model_catalog_current(&chat->model_catalog);
    chat->model = chat->selected_model->id;
    return true;
}

static bool initialize_chat(struct chat_session *chat,
                            const struct telos_config *config,
                            const char *home_directory,
                            const char *current_directory,
                            struct telos_error **error)
{
    const char *provider_name = telos_config_get(config, "agent.provider");
    const char *model = telos_config_get(config, "agent.model");
    const char *endpoint = telos_config_get(config, "agent.endpoint");
    const char *authentication_endpoint =
        getenv("TELOS_OPENAI_AUTH_ENDPOINT");
    char authentication_directory[CHAT_PATH_SIZE];

    if (provider_name == NULL ||
        (strcmp(provider_name, "openai") != 0 &&
         strcmp(provider_name, "openai-responses") != 0 &&
         strcmp(provider_name, "dev.zevorn.openai-responses") != 0 &&
         strcmp(provider_name, "deepseek") != 0 &&
         strcmp(provider_name, "zai") != 0)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "The configured Agent Provider is not available");
        return false;
    }
    if (!endpoint_is_allowed(endpoint)) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "Agent endpoint must use HTTPS or loopback HTTP");
        return false;
    }
    chat->loopback_endpoint = endpoint_is_loopback(endpoint);
    chat->loopback_authentication =
        authentication_endpoint != NULL &&
        endpoint_is_loopback(authentication_endpoint);
    telos_model_catalog_initialize(&chat->model_catalog);
    if (!telos_official_model_catalog_add(&chat->model_catalog, error) ||
        !select_configured_model(chat, provider_name, model, error) ||
        !register_chat_commands(chat, error)) {
        return false;
    }
    chat->configured_endpoint = endpoint;
    chat->transport.timeout_milliseconds =
        TELOS_CURL_DEFAULT_TIMEOUT_MILLISECONDS;
    if (strcmp(chat->configured_provider, "openai") == 0) {
        if (snprintf(authentication_directory, sizeof(authentication_directory),
                     "%s/.telos", home_directory) >=
            (int)sizeof(authentication_directory)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Agent authentication state path is too long");
            return false;
        }
        chat->authentication_definition =
            telos_openai_codex_authentication_definition();
        {
            const struct telos_authentication_config authentication_config = {
                .state_directory = authentication_directory,
                .service_endpoint = authentication_endpoint,
                .send = telos_curl_transport_send,
                .transport_context = &chat->transport,
            };

            chat->authentication = chat->authentication_definition->create(
                &authentication_config, error);
        }
        if (chat->authentication == NULL) {
            return false;
        }
    }
    chat->secret_broker = telos_secret_broker_create(resolve_secret, chat,
                                                      error);
    if (chat->secret_broker == NULL) {
        return false;
    }
    if (chat->model != NULL && !create_provider(chat, error)) {
        return false;
    }
    chat->registry = telos_registry_create(NULL, 0, error);
    if (chat->registry == NULL) {
        return false;
    }
    chat->generation = telos_registry_acquire(chat->registry);
    if (chat->generation == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent Registry Snapshot allocation failed");
        return false;
    }
    chat->capability_broker =
        telos_capability_broker_create(NULL, 0, deny_tools, NULL, error);
    if (chat->capability_broker == NULL) {
        return false;
    }
    chat->tools = telos_value_new_array(NULL, 0);
    chat->provider_options = telos_value_new_object(NULL, NULL, 0);
    if (chat->tools == NULL || chat->provider_options == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent Provider options allocation failed");
        return false;
    }
    return create_prompt(chat, home_directory, current_directory, error);
}

static void clear_chat(struct chat_session *chat)
{
    clear_messages(chat);
    telos_value_release(chat->provider_options);
    telos_value_release(chat->tools);
    telos_prompt_snapshot_release(chat->prompt);
    telos_capability_broker_destroy(chat->capability_broker);
    telos_registry_generation_release(chat->generation);
    telos_registry_destroy(chat->registry);
    if (chat->provider_destroy != NULL) {
        chat->provider_destroy(chat->provider_context);
    }
    telos_secret_broker_destroy(chat->secret_broker);
    if (chat->authentication_definition != NULL) {
        chat->authentication_definition->destroy(chat->authentication);
    }
}

bool telos_chat_run(const struct telos_config *config,
                    const char *home_directory,
                    const char *current_directory,
                    const char *initial_prompt,
                    bool single_turn,
                    struct telos_error **error)
{
    struct chat_session chat = {0};
    const char *provider;
    const char *model;
    bool result;

    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || home_directory == NULL ||
        current_directory == NULL ||
        (single_turn && (initial_prompt == NULL ||
                         initial_prompt[0] == '\0'))) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent chat arguments are invalid");
        return false;
    }
    if (!initialize_chat(&chat, config, home_directory, current_directory,
                         error)) {
        clear_chat(&chat);
        return false;
    }
    provider = telos_config_get(config, "agent.provider");
    model = chat.model == NULL ? "not configured" : chat.model;
    {
        const struct telos_frontend_session session = {
            .application = "Telos",
            .version = "0.1.0",
            .provider = provider,
            .model = model,
            .working_directory = current_directory,
            .command_help =
                "/model  select model · /login-status  show auth status",
            .initial_prompt = initial_prompt,
            .commands = &chat.commands,
            .provider_get = chat_provider_get,
            .model_get = chat_model_get,
            .identity_context = &chat,
            .single_turn = single_turn,
            .turn = chat_turn,
            .turn_context = &chat,
        };
        const struct telos_terminal_frontend_config frontend = {
            .session = &session,
            .input_descriptor = STDIN_FILENO,
            .output_descriptor = STDOUT_FILENO,
            .force_plain = single_turn,
        };

        result = telos_terminal_frontend_run(&frontend, error);
    }
    clear_chat(&chat);
    return result;
}
