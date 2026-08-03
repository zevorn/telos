#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/agent.h>
#include <telos/authentication.h>
#include <telos/clock.h>
#include <telos/command.h>
#include <telos/event.h>
#include <telos/id.h>
#include <telos/model.h>
#include <telos/plugins/anthropic.h>
#include <telos/plugins/api_key_auth.h>
#include <telos/plugins/curl_transport.h>
#include <telos/plugins/jsonl_store.h>
#include <telos/plugins/model_catalog.h>
#include <telos/plugins/openai_codex_auth.h>
#include <telos/plugins/openai_chat.h>
#include <telos/plugins/openai_responses.h>
#include <telos/plugins/project_guidance.h>
#include <telos/plugins/posix_tools.h>
#include <telos/plugins/terminal_frontend.h>
#include <telos/prompt.h>
#include <telos/registry.h>
#include <telos/secret.h>
#include <telos/store.h>
#include <telos/tool.h>
#include <telos/value.h>

#include "chat.h"

#define CHAT_PATH_SIZE 4096U
#define CHAT_MAXIMUM_MESSAGES 64U
#define CHAT_MAXIMUM_CONVERSATION_BYTES (4U * 1024U * 1024U)
#define CHAT_MAXIMUM_RESPONSE_BYTES (1024U * 1024U)
#define CHAT_SESSION_NAME_SIZE 128U
#define CHAT_SESSION_DIRECTORY_SIZE CHAT_PATH_SIZE
#define CHAT_THINKING_LEVEL_SIZE 16U
#define CHAT_MODEL_ID_SIZE 256U
#define CHAT_AUTHENTICATION_CAPACITY 4U

struct chat_authentication_slot {
    const char *provider;
    const struct telos_authentication_definition_v1 *definition;
    struct telos_authentication *authentication;
};

struct chat_session {
    const struct telos_authentication_definition_v1 *authentication_definition;
    struct telos_authentication *authentication;
    struct telos_secret_broker *secret_broker;
    void *provider_context;
    telos_provider_dispatch_fn provider_dispatch;
    void (*provider_destroy)(void *provider);
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_posix_tools *posix_tools;
    struct telos_capability_broker *capability_broker;
    struct telos_prompt_snapshot *prompt;
    struct telos_value *tools;
    struct telos_value *provider_options;
    struct telos_event_store *session_store;
    struct telos_id session_id;
    uint64_t session_sequence;
    char session_path[CHAT_SESSION_DIRECTORY_SIZE];
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
    char model_storage[TELOS_MODEL_CATALOG_CAPACITY][CHAT_MODEL_ID_SIZE];
    struct telos_command_registry commands;
    const struct telos_model_descriptor *selected_model;
    const char *configured_provider;
    const char *home_directory;
    const char *current_directory;
    char authentication_directory[CHAT_PATH_SIZE];
    const char *authentication_endpoint;
    struct chat_authentication_slot authentications[
        CHAT_AUTHENTICATION_CAPACITY];
    size_t authentication_count;
    char thinking_level[CHAT_THINKING_LEVEL_SIZE];
    char session_name[CHAT_SESSION_NAME_SIZE];
    struct telos_value *checkpoint[CHAT_MAXIMUM_MESSAGES];
    size_t checkpoint_count;
};

struct observer_context {
    telos_frontend_emit_fn emit;
    void *emit_context;
    const char *provider;
};

static void drop_messages(struct chat_session *chat, size_t count);
static void clear_messages(struct chat_session *chat);
static bool create_prompt(struct chat_session *chat,
                          const char *home_directory,
                          const char *current_directory,
                          struct telos_error **error);
static bool find_project_root(const char *current_directory,
                              char root[CHAT_PATH_SIZE]);
static struct telos_value *read_jsonl_session_file(const char *path,
                                                   struct telos_error **error);
static bool persist_session_marker(struct chat_session *chat,
                                   const char *type,
                                   struct telos_error **error);
static bool ensure_authentication(struct chat_session *chat,
                                  const char *provider,
                                  struct telos_error **error);

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool make_directory(const char *path, struct telos_error **error)
{
    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Session directory could not be created");
        return false;
    }
    return true;
}

static bool initialize_session_store(struct chat_session *chat,
                                     const char *home_directory,
                                     struct telos_error **error)
{
    char directory[CHAT_SESSION_DIRECTORY_SIZE];
    char identifier[TELOS_ID_TEXT_SIZE];

    chat->session_id = telos_id_generate();
    if (!telos_id_format(chat->session_id, identifier, sizeof(identifier)) ||
        snprintf(directory, sizeof(directory), "%s/.telos", home_directory) >=
            (int)sizeof(directory) ||
        !make_directory(directory, error) ||
        snprintf(directory, sizeof(directory), "%s/.telos/sessions",
                 home_directory) >= (int)sizeof(directory) ||
        !make_directory(directory, error) ||
        snprintf(chat->session_path, sizeof(chat->session_path),
                 "%s/%s.jsonl", directory, identifier) >=
            (int)sizeof(chat->session_path)) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Session path is too long");
        }
        return false;
    }
    chat->session_store = telos_jsonl_store_create(chat->session_path, error);
    if (chat->session_store == NULL) {
        return false;
    }
    chat->session_sequence = telos_event_store_count(chat->session_store);
    return true;
}

static bool persist_session_value(struct chat_session *chat,
                                  const char *type,
                                  const struct telos_value *payload,
                                  struct telos_error **error)
{
    struct telos_id event_id;
    struct telos_event_spec spec;
    struct telos_event *event;
    struct telos_clock clock = telos_system_clock();
    struct telos_error *clock_error = NULL;
    int64_t timestamp = 0;
    bool result;

    if (chat->session_store == NULL) {
        return true;
    }
    if (!telos_clock_now_milliseconds(&clock, &timestamp, &clock_error)) {
        if (error != NULL && *error == NULL) {
            *error = clock_error;
            clock_error = NULL;
        }
        telos_error_release(clock_error);
        return false;
    }
    event_id = telos_id_generate();
    spec = (struct telos_event_spec){
        .sequence = ++chat->session_sequence,
        .event_id = event_id,
        .session_id = chat->session_id,
        .correlation_id = event_id,
        .causation_id = event_id,
        .type = type,
        .source = "telos-terminal",
        .timestamp_milliseconds = timestamp,
        .payload = payload,
    };
    event = telos_event_create(&spec, error);
    if (event == NULL) {
        --chat->session_sequence;
        return false;
    }
    result = telos_event_store_append(chat->session_store, event, error);
    telos_event_release(event);
    if (!result) {
        --chat->session_sequence;
    }
    return result;
}

static bool persist_session_marker(struct chat_session *chat,
                                   const char *type,
                                   struct telos_error **error)
{
    struct telos_value *payload = telos_value_new_object(NULL, NULL, 0);
    bool result;

    if (payload == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session marker allocation failed");
        return false;
    }
    result = persist_session_value(chat, type, payload, error);
    telos_value_release(payload);
    return result;
}

static bool persist_session_snapshot(struct chat_session *chat,
                                     struct telos_error **error)
{
    if (!persist_session_marker(chat, "session.reset", error)) {
        return false;
    }
    for (size_t index = 0; index < chat->message_count; ++index) {
        if (!persist_session_value(chat, "message", chat->messages[index],
                                   error)) {
            return false;
        }
    }
    return true;
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

static const char *provider_display_name(const char *provider)
{
    if (provider == NULL) {
        return "Provider";
    }
    if (strcmp(provider, "openai") == 0) {
        return "OpenAI";
    }
    if (strcmp(provider, "deepseek") == 0) {
        return "DeepSeek";
    }
    if (strcmp(provider, "zai") == 0 || strcmp(provider, "z.ai") == 0 ||
        strcmp(provider, "z-ai") == 0) {
        return "Z.AI";
    }
    if (strcmp(provider, "anthropic") == 0) {
        return "Anthropic";
    }
    return provider;
}

static const char *canonical_provider(const char *provider)
{
    if (provider == NULL) {
        return NULL;
    }
    if (strcmp(provider, "openai-responses") == 0 ||
        strcmp(provider, "openai-chat") == 0 ||
        strcmp(provider, "dev.zevorn.openai-responses") == 0 ||
        strcmp(provider, "dev.zevorn.openai-chat") == 0 ||
        strcmp(provider, "openai-codex") == 0) {
        return "openai";
    }
    if (strcmp(provider, "z.ai") == 0 || strcmp(provider, "z-ai") == 0 ||
        strcmp(provider, "glm") == 0) {
        return "zai";
    }
    if (strcmp(provider, "claude") == 0) {
        return "anthropic";
    }
    return provider;
}

static const struct telos_authentication_definition_v1 *
authentication_definition_for(const char *provider)
{
    if (strcmp(provider, "openai") == 0) {
        return telos_openai_codex_authentication_definition();
    }
    if (strcmp(provider, "deepseek") == 0) {
        return telos_deepseek_api_key_authentication_definition();
    }
    if (strcmp(provider, "zai") == 0) {
        return telos_zai_api_key_authentication_definition();
    }
    if (strcmp(provider, "anthropic") == 0) {
        return telos_anthropic_api_key_authentication_definition();
    }
    return NULL;
}

static bool ensure_authentication(struct chat_session *chat,
                                  const char *provider,
                                  struct telos_error **error)
{
    const struct telos_authentication_definition_v1 *definition;
    const char *canonical = canonical_provider(provider);

    if (chat == NULL || canonical == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Authentication Provider is invalid");
        return false;
    }
    for (size_t index = 0; index < chat->authentication_count; ++index) {
        struct chat_authentication_slot *slot =
            &chat->authentications[index];

        if (strcmp(slot->provider, canonical) == 0) {
            chat->authentication_definition = slot->definition;
            chat->authentication = slot->authentication;
            return true;
        }
    }
    if (chat->authentication_count >= CHAT_AUTHENTICATION_CAPACITY) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOSPC,
                  "Authentication Provider capacity is exhausted");
        return false;
    }
    definition = authentication_definition_for(canonical);
    if (definition == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "Authentication Provider is not available");
        return false;
    }
    {
        const struct telos_authentication_config config = {
            .state_directory = chat->authentication_directory,
            .service_endpoint = chat->authentication_endpoint,
            .send = telos_curl_transport_send,
            .transport_context = &chat->transport,
        };
        struct telos_authentication *authentication = definition->create(
            &config, error);

        if (authentication == NULL) {
            return false;
        }
        chat->authentications[chat->authentication_count++] =
            (struct chat_authentication_slot){
                .provider = canonical,
                .definition = definition,
                .authentication = authentication,
            };
        chat->authentication_definition = definition;
        chat->authentication = authentication;
    }
    return true;
}

static enum telos_model_api model_api_for_provider(const char *provider)
{
    if (strcmp(provider, "openai") == 0) {
        return TELOS_MODEL_API_OPENAI_RESPONSES;
    }
    if (strcmp(provider, "anthropic") == 0) {
        return TELOS_MODEL_API_ANTHROPIC_MESSAGES;
    }
    return TELOS_MODEL_API_OPENAI_CHAT;
}

static bool copy_trimmed(const char *source, char *target, size_t capacity,
                         struct telos_error **error)
{
    const char *start = source;
    size_t size;

    if (source == NULL || target == NULL || capacity == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Text argument is invalid");
        return false;
    }
    while (isspace((unsigned char)*start)) {
        ++start;
    }
    size = strlen(start);
    while (size > 0 && isspace((unsigned char)start[size - 1])) {
        --size;
    }
    if (size >= capacity) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Text argument is too long");
        return false;
    }
    memcpy(target, start, size);
    target[size] = '\0';
    return true;
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

static bool emit_notice(telos_frontend_emit_fn emit,
                        void *emit_context,
                        const char *text,
                        struct telos_error **error)
{
    const struct telos_frontend_event event = {
        .kind = TELOS_FRONTEND_NOTICE,
        .text = text,
    };

    return emit(&event, emit_context, error);
}

static void clear_checkpoint(struct chat_session *chat)
{
    for (size_t index = 0; index < chat->checkpoint_count; ++index) {
        telos_value_release(chat->checkpoint[index]);
        chat->checkpoint[index] = NULL;
    }
    chat->checkpoint_count = 0;
}

static bool checkpoint_messages(struct chat_session *chat,
                                struct telos_error **error)
{
    clear_checkpoint(chat);
    for (size_t index = 0; index < chat->message_count; ++index) {
        chat->checkpoint[index] = telos_value_retain(chat->messages[index]);
        if (chat->checkpoint[index] == NULL) {
            clear_checkpoint(chat);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Session checkpoint allocation failed");
            return false;
        }
        chat->checkpoint_count += 1;
    }
    return true;
}

static void restore_checkpoint(struct chat_session *chat)
{
    clear_messages(chat);
    for (size_t index = 0; index < chat->checkpoint_count; ++index) {
        const struct telos_value *message = chat->checkpoint[index];
        const char *content = telos_value_string(
            telos_value_get(message, "content"));
        size_t size = content == NULL ? telos_value_json_size(message)
                                      : strlen(content);

        chat->messages[index] = telos_value_retain(message);
        chat->message_sizes[index] = size;
        chat->message_count += 1;
        chat->conversation_bytes += size;
    }
}

static bool replace_messages(struct chat_session *chat,
                             const struct telos_value *items,
                             struct telos_error **error)
{
    struct telos_value *owned[CHAT_MAXIMUM_MESSAGES] = {0};
    size_t count;
    size_t bytes = 0;

    if (items == NULL || telos_value_type(items) != TELOS_VALUE_ARRAY ||
        telos_value_count(items) > CHAT_MAXIMUM_MESSAGES) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Imported session messages are invalid");
        return false;
    }
    count = telos_value_count(items);
    for (size_t index = 0; index < count; ++index) {
        const struct telos_value *message = telos_value_at(items, index);
        const char *content;
        size_t size;

        if (message == NULL ||
            telos_value_type(message) != TELOS_VALUE_OBJECT) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Imported session message is not an object");
            goto failure;
        }
        owned[index] = telos_value_retain(message);
        content = telos_value_string(telos_value_get(message, "content"));
        size = content == NULL ? telos_value_json_size(message)
                               : strlen(content);
        if (size > CHAT_MAXIMUM_CONVERSATION_BYTES - bytes) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EFBIG,
                      "Imported session exceeds the conversation limit");
            goto failure;
        }
        bytes += size;
    }
    clear_messages(chat);
    for (size_t index = 0; index < count; ++index) {
        const char *content =
            telos_value_string(telos_value_get(owned[index], "content"));

        chat->messages[index] = owned[index];
        chat->message_sizes[index] =
            content == NULL ? telos_value_json_size(owned[index])
                            : strlen(content);
        chat->message_count += 1;
        chat->conversation_bytes += chat->message_sizes[index];
        owned[index] = NULL;
    }
    return true;

failure:
    for (size_t index = 0; index < count; ++index) {
        telos_value_release(owned[index]);
    }
    return false;
}

static bool write_session_file(const struct chat_session *chat,
                               const char *path,
                               struct telos_error **error)
{
    struct telos_value *items = telos_value_new_array(
        (const struct telos_value *const *)chat->messages,
        chat->message_count);
    size_t size;
    char *json;
    FILE *stream;
    bool result = false;

    if (items == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session export allocation failed");
        return false;
    }
    size = telos_value_json_size(items);
    json = malloc(size);
    stream = json == NULL ? NULL : fopen(path, "wb");
    if (json == NULL || stream == NULL ||
        !telos_value_write_json(items, json, size, NULL, error) ||
        fwrite(json, 1, size - 1, stream) != size - 1 ||
        fputc('\n', stream) == EOF) {
        if (stream != NULL) {
            fclose(stream);
            stream = NULL;
        }
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Session export failed");
        }
        goto cleanup;
    }
    if (fclose(stream) != 0) {
        stream = NULL;
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Session export could not be closed");
        goto cleanup;
    }
    stream = NULL;
    result = true;

cleanup:
    free(json);
    telos_value_release(items);
    return result;
}

static struct telos_value *read_session_file(const char *path,
                                             struct telos_error **error)
{
    FILE *stream = fopen(path, "rb");
    long length;
    char *json;
    struct telos_value *items;

    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Session import file could not be read");
        return NULL;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Session import file could not be read");
        return NULL;
    }
    length = ftell(stream);
    if (length < 0 || (size_t)length > CHAT_MAXIMUM_CONVERSATION_BYTES ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Session import file could not be read");
        return NULL;
    }
    json = malloc((size_t)length + 1);
    if (json == NULL || fread(json, 1, (size_t)length, stream) !=
                             (size_t)length ||
        fclose(stream) != 0) {
        free(json);
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Session import file could not be read");
        return NULL;
    }
    json[length] = '\0';
    items = telos_value_parse_json(json, (size_t)length, error);
    free(json);
    if (items == NULL) {
        if (error != NULL) {
            telos_error_release(*error);
            *error = NULL;
        }
        return read_jsonl_session_file(path, error);
    }
    return items;
}

static void release_session_items(struct telos_value **items, size_t *count)
{
    for (size_t index = 0; index < *count; ++index) {
        telos_value_release(items[index]);
        items[index] = NULL;
    }
    *count = 0;
}

static bool append_loaded_session_item(struct telos_value **items,
                                       size_t *count,
                                       const struct telos_value *value,
                                       struct telos_error **error)
{
    if (*count >= CHAT_MAXIMUM_MESSAGES) {
        telos_value_release(items[0]);
        memmove(items, items + 1,
                (CHAT_MAXIMUM_MESSAGES - 1) * sizeof(items[0]));
        *count = CHAT_MAXIMUM_MESSAGES - 1;
    }
    items[*count] = telos_value_retain(value);
    if (items[*count] == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session recovery allocation failed");
        return false;
    }
    *count += 1;
    return true;
}

static struct telos_value *read_jsonl_session_file(const char *path,
                                                   struct telos_error **error)
{
    struct telos_event_store *store = NULL;
    struct telos_value *items[CHAT_MAXIMUM_MESSAGES] = {0};
    struct telos_value *result = NULL;
    size_t count = 0;

    store = telos_jsonl_store_create(path, error);
    if (store == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < telos_event_store_count(store); ++index) {
        struct telos_event *event = telos_event_store_get(store, index, error);
        const char *type;

        if (event == NULL) {
            goto cleanup;
        }
        type = telos_event_type(event);
        if (strcmp(type, "session.reset") == 0) {
            release_session_items(items, &count);
        } else if (strcmp(type, "message") == 0) {
            const struct telos_value *payload = telos_event_payload(event);
            const char *role = telos_value_string(
                telos_value_get(payload, "role"));
            const char *content = telos_value_string(
                telos_value_get(payload, "content"));

            if (payload == NULL || telos_value_type(payload) !=
                                       TELOS_VALUE_OBJECT ||
                role == NULL || content == NULL ||
                !append_loaded_session_item(items, &count, payload, error)) {
                telos_event_release(event);
                set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "JSONL session message is invalid");
                goto cleanup;
            }
        }
        telos_event_release(event);
    }
    result = telos_value_new_array(
        (const struct telos_value *const *)items, count);
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session recovery array allocation failed");
    }

cleanup:
    release_session_items(items, &count);
    telos_event_store_destroy(store);
    return result;
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
    if (chat->authentication_definition != NULL &&
        chat->authentication != NULL &&
        (!chat->loopback_endpoint || chat->loopback_authentication)) {
        struct telos_authentication_status status;

        if (chat->authentication_definition->status(chat->authentication,
                                                     &status, error) &&
            status.state == TELOS_AUTHENTICATION_SIGNED_IN &&
            status.provider != NULL &&
            (strcmp(status.provider, provider) == 0 ||
             (strcmp(provider, "openai") == 0 &&
              strcmp(status.provider, "openai-codex") == 0))) {
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
    } else if (strcmp(provider, "anthropic") == 0) {
        value = getenv("ANTHROPIC_API_KEY");
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
    const struct telos_transport_header anthropic_headers[] = {
        {
            .name = "anthropic-version",
            .value = "2023-06-01",
        },
    };
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
    } else if (strcmp(provider_name, "anthropic") == 0) {
        secret_provider = "anthropic";
        if (strcmp(endpoint, "https://api.openai.com/v1") == 0) {
            endpoint = "https://api.anthropic.com/v1";
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
    } else if (model->api == TELOS_MODEL_API_ANTHROPIC_MESSAGES) {
        const struct telos_anthropic_config config = {
            .model = chat->model,
            .endpoint = endpoint,
            .secret_reference = secret_reference,
            .secret_target = secret_reference + sizeof("secret:") - 1,
            .secret_broker = chat->secret_broker,
            .capabilities = capabilities,
            .capability_count = 2,
            .headers = anthropic_headers,
            .header_count = 1,
            .send = telos_curl_transport_send,
            .transport_context = &chat->transport,
            .unknown_event_policy = TELOS_ANTHROPIC_UNKNOWN_EVENT_IGNORE,
        };

        provider_context = telos_anthropic_provider_create(&config, error);
        provider_dispatch = telos_anthropic_provider_dispatch;
        provider_destroy = (void (*)(void *))telos_anthropic_provider_destroy;
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
allow_tools(const struct telos_policy_request *request, void *context)
{
    const char *disable_bash = getenv("TELOS_AGENT_DISABLE_BASH");

    (void)context;

    if (request == NULL || request->tool_id == NULL) {
        return TELOS_POLICY_DENY;
    }
    if (strcmp(request->tool_id, "bash") == 0 && disable_bash != NULL &&
        strcmp(disable_bash, "1") == 0) {
        return TELOS_POLICY_DENY;
    }
    return TELOS_POLICY_ALLOW;
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
    if (!persist_session_value(chat, "message", message, error)) {
        chat->message_count -= 1;
        chat->conversation_bytes -= size;
        chat->messages[chat->message_count] = NULL;
        chat->message_sizes[chat->message_count] = 0;
        telos_value_release(message);
        return false;
    }
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
    const char *provider = provider_display_name(observer->provider);

    if (event->kind == TELOS_AUTHENTICATION_VERIFICATION_REQUIRED) {
        if (event->verification_uri == NULL || event->user_code == NULL ||
            snprintf(message, sizeof(message),
                     "Open %s and enter code %s",
                     event->verification_uri, event->user_code) >=
                (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Authentication verification instructions are invalid");
            return false;
        }
        return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                             error);
    }
    if (event->kind == TELOS_AUTHENTICATION_COMPLETED) {
        if (snprintf(message, sizeof(message), "%s login completed",
                     provider) >= (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Authentication completion message is too long");
            return false;
        }
        return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                             error);
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
    if (chat->model == NULL || chat->selected_model == NULL) {
        return true;
    }
    for (size_t index = 0; index < chat->authentication_count; ++index) {
        const struct chat_authentication_slot *slot =
            &chat->authentications[index];

        if (slot->authentication == chat->authentication) {
            return strcmp(chat->selected_model->provider, slot->provider) !=
                       0 ||
                   create_provider(chat, error);
        }
    }
    return true;
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
    if (chat->model != NULL && chat->selected_model != NULL) {
        for (size_t index = 0; index < chat->authentication_count; ++index) {
            const struct chat_authentication_slot *slot =
                &chat->authentications[index];

            if (slot->authentication == chat->authentication &&
                strcmp(slot->provider, chat->selected_model->provider) == 0 &&
                !create_provider(chat, error)) {
                return false;
            }
        }
    }
    {
        char message[128];
        const char *provider = observer->provider == NULL
                                   ? chat_provider_get(chat)
                                   : observer->provider;

        if (snprintf(message, sizeof(message), "%s logout completed",
                     provider_display_name(provider)) >=
            (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Authentication logout message is too long");
            return false;
        }
        return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                             error);
    }
}

static bool login_status_command(const struct chat_session *chat,
                                 struct observer_context *observer,
                                 struct telos_error **error)
{
    struct telos_authentication_status status;
    char message[512];
    const char *provider = observer->provider == NULL
                               ? chat_provider_get((void *)chat)
                               : observer->provider;

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
        if (strcmp(canonical_provider(provider), "openai") == 0) {
            if (snprintf(message, sizeof(message),
                         "%s is logged in as account %s",
                         provider_display_name(provider),
                         status.account_id == NULL ? "unknown"
                                                   : status.account_id) >=
                (int)sizeof(message)) {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                          "Authentication account identifier is too long");
                return false;
            }
        } else if (snprintf(message, sizeof(message), "%s is logged in",
                            provider_display_name(provider)) >=
                   (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Authentication status message is too long");
            return false;
        }
    } else if (status.state == TELOS_AUTHENTICATION_AUTHORIZING) {
        if (snprintf(message, sizeof(message), "%s login is in progress",
                     provider_display_name(provider)) >=
            (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Authentication status message is too long");
            return false;
        }
    } else if (snprintf(
                   message, sizeof(message), "%s is logged out",
                   provider_display_name(provider)) >=
               (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Authentication status message is too long");
        return false;
    }
    return emit_frontend(observer, TELOS_FRONTEND_NOTICE, message, NULL,
                         error);
}

struct model_list_context {
    char text[TELOS_COMMAND_ARGUMENT_SIZE];
    const char *provider_filter;
    size_t used;
};

static bool append_model_list(const struct telos_model_descriptor *model,
                              void *context, struct telos_error **error)
{
    struct model_list_context *list = context;
    int written;

    (void)error;
    if (list->provider_filter != NULL &&
        strcmp(list->provider_filter, model->provider) != 0) {
        return true;
    }
    if (model->reasoning != NULL) {
        written = snprintf(list->text + list->used,
                           sizeof(list->text) - list->used, "%s/%s "
                                                           "(reasoning=%s)\n",
                           model->provider, model->id, model->reasoning);
    } else {
        written = snprintf(list->text + list->used,
                           sizeof(list->text) - list->used, "%s/%s\n",
                           model->provider, model->id);
    }
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
    if (!persist_session_marker(chat, "session.reset", error)) {
        return false;
    }
    clear_messages(chat);
    return emit_frontend(&observer, TELOS_FRONTEND_NOTICE,
                         "conversation cleared", NULL, error);
}

static bool new_command(const char *arguments,
                        const struct telos_cancel *cancel,
                        telos_frontend_emit_fn emit,
                        void *emit_context,
                        void *context,
                        struct telos_error **error)
{
    struct chat_session *chat = context;

    (void)arguments;
    (void)cancel;
    if (!persist_session_marker(chat, "session.reset", error)) {
        return false;
    }
    clear_messages(chat);
    chat->session_name[0] = '\0';
    return emit_notice(emit, emit_context, "new session started", error);
}

static bool name_command(const char *arguments,
                         const struct telos_cancel *cancel,
                         telos_frontend_emit_fn emit,
                         void *emit_context,
                         void *context,
                         struct telos_error **error)
{
    struct chat_session *chat = context;
    char message[CHAT_SESSION_NAME_SIZE + 32];

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        if (chat->session_name[0] == '\0') {
            return emit_notice(emit, emit_context, "session is unnamed", error);
        }
        if (snprintf(message, sizeof(message), "session name: %s",
                     chat->session_name) >= (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Session name is too long");
            return false;
        }
        return emit_notice(emit, emit_context, message, error);
    }
    if (strlen(arguments) >= sizeof(chat->session_name)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Session name is too long");
        return false;
    }
    memcpy(chat->session_name, arguments, strlen(arguments) + 1);
    if (snprintf(message, sizeof(message), "session named: %s",
                 chat->session_name) >= (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Session name is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool session_command(const char *arguments,
                            const struct telos_cancel *cancel,
                            telos_frontend_emit_fn emit,
                            void *emit_context,
                            void *context,
                            struct telos_error **error)
{
    struct chat_session *chat = context;
    char message[256];

    (void)arguments;
    (void)cancel;
    if (snprintf(message, sizeof(message),
                 "session %s · %zu messages · %zu bytes · %s",
                 chat->session_name[0] == '\0' ? "unnamed" : chat->session_name,
                 chat->message_count, chat->conversation_bytes,
                 chat->session_path[0] == '\0' ? "not persisted"
                                               : chat->session_path) >=
        (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Session summary is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool sessions_command(const char *arguments,
                             const struct telos_cancel *cancel,
                             telos_frontend_emit_fn emit,
                             void *emit_context,
                             void *context,
                             struct telos_error **error)
{
    struct chat_session *chat = context;
    char directory[CHAT_PATH_SIZE];
    char text[TELOS_COMMAND_ARGUMENT_SIZE] = "saved sessions:\n";
    size_t used = sizeof("saved sessions:\n") - 1;
    const char *separator;
    DIR *stream;
    struct dirent *entry;
    size_t count = 0;

    (void)arguments;
    (void)cancel;
    separator = strrchr(chat->session_path, '/');
    if (separator == NULL ||
        (size_t)(separator - chat->session_path) >= sizeof(directory)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session directory is invalid");
        return false;
    }
    memcpy(directory, chat->session_path,
           (size_t)(separator - chat->session_path));
    directory[separator - chat->session_path] = '\0';
    stream = opendir(directory);
    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Session directory could not be opened");
        return false;
    }
    while ((entry = readdir(stream)) != NULL && count < 32U) {
        size_t name_size = strlen(entry->d_name);
        int written;

        if (name_size < sizeof(".jsonl") - 1 ||
            strcmp(entry->d_name + name_size - (sizeof(".jsonl") - 1),
                   ".jsonl") != 0) {
            continue;
        }
        written = snprintf(text + used, sizeof(text) - used, "  %s%s\n",
                           entry->d_name,
                           strcmp(entry->d_name,
                                  strrchr(chat->session_path, '/') + 1) == 0
                               ? " (current)"
                               : "");
        if (written < 0 || (size_t)written >= sizeof(text) - used) {
            closedir(stream);
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Session list is too large");
            return false;
        }
        used += (size_t)written;
        ++count;
    }
    closedir(stream);
    if (count == 0) {
        memcpy(text, "no saved sessions", sizeof("no saved sessions"));
    }
    return emit_notice(emit, emit_context, text, error);
}

static bool load_latest_session(struct chat_session *chat,
                                struct telos_error **error)
{
    char directory[CHAT_PATH_SIZE];
    char latest[CHAT_PATH_SIZE] = {0};
    const char *separator = strrchr(chat->session_path, '/');
    struct stat latest_status = {0};
    DIR *stream;
    struct dirent *entry;
    bool found = false;
    struct telos_value *items;

    if (separator == NULL ||
        (size_t)(separator - chat->session_path) >= sizeof(directory)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session directory is invalid");
        return false;
    }
    memcpy(directory, chat->session_path,
           (size_t)(separator - chat->session_path));
    directory[separator - chat->session_path] = '\0';
    stream = opendir(directory);
    if (stream == NULL) {
        if (errno == ENOENT) {
            return true;
        }
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Session directory could not be opened");
        return false;
    }
    while ((entry = readdir(stream)) != NULL) {
        char path[CHAT_PATH_SIZE];
        struct stat status;
        size_t name_size = strlen(entry->d_name);

        if (name_size < sizeof(".jsonl") - 1 ||
            strcmp(entry->d_name + name_size - (sizeof(".jsonl") - 1),
                   ".jsonl") != 0 ||
            strcmp(entry->d_name, separator + 1) == 0 ||
            snprintf(path, sizeof(path), "%s/%s", directory,
                     entry->d_name) >= (int)sizeof(path) ||
            stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
            continue;
        }
        if (!found || status.st_mtime > latest_status.st_mtime) {
            memcpy(latest, path, strlen(path) + 1);
            latest_status = status;
            found = true;
        }
    }
    closedir(stream);
    if (!found) {
        return true;
    }
    items = read_session_file(latest, error);
    if (items == NULL || !replace_messages(chat, items, error)) {
        telos_value_release(items);
        return false;
    }
    telos_value_release(items);
    return persist_session_snapshot(chat, error) &&
           checkpoint_messages(chat, error);
}

static bool tree_command(const char *arguments,
                         const struct telos_cancel *cancel,
                         telos_frontend_emit_fn emit,
                         void *emit_context,
                         void *context,
                         struct telos_error **error)
{
    struct chat_session *chat = context;
    char text[TELOS_COMMAND_ARGUMENT_SIZE] = {0};
    size_t used = 0;

    (void)arguments;
    (void)cancel;
    for (size_t index = 0; index < chat->message_count; ++index) {
        const struct telos_value *message = chat->messages[index];
        const char *role = telos_value_string(telos_value_get(message, "role"));
        const char *content =
            telos_value_string(telos_value_get(message, "content"));
        int written = snprintf(text + used, sizeof(text) - used,
                               "%zu: %s%s%s\n", index,
                               role == NULL ? "item" : role,
                               content == NULL ? "" : " ",
                               content == NULL ? "" : content);

        if (written < 0 || (size_t)written >= sizeof(text) - used) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Session tree is too large");
            return false;
        }
        used += (size_t)written;
    }
    if (used == 0) {
        memcpy(text, "session tree is empty", sizeof("session tree is empty"));
    }
    return emit_notice(emit, emit_context, text, error);
}

static bool compact_command(const char *arguments,
                            const struct telos_cancel *cancel,
                            telos_frontend_emit_fn emit,
                            void *emit_context,
                            void *context,
                            struct telos_error **error)
{
    struct chat_session *chat = context;
    size_t keep = 8;

    (void)arguments;
    (void)cancel;
    if (chat->message_count > keep) {
        drop_messages(chat, chat->message_count - keep);
    }
    if (!persist_session_snapshot(chat, error)) {
        return false;
    }
    return emit_notice(emit, emit_context, "conversation compacted", error);
}

static bool fork_command(const char *arguments,
                         const struct telos_cancel *cancel,
                         telos_frontend_emit_fn emit,
                         void *emit_context,
                         void *context,
                         struct telos_error **error)
{
    struct chat_session *chat = context;
    const char *name = arguments == NULL || arguments[0] == '\0'
                           ? "fork"
                           : arguments;

    (void)cancel;
    if (!checkpoint_messages(chat, error)) {
        return false;
    }
    if (strlen(name) >= sizeof(chat->session_name)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Fork name is too long");
        return false;
    }
    memcpy(chat->session_name, name, strlen(name) + 1);
    return emit_notice(emit, emit_context, "session fork checkpoint saved",
                       error);
}

static bool resume_command(const char *arguments,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *context,
                           struct telos_error **error)
{
    struct chat_session *chat = context;
    struct telos_value *items = NULL;
    bool result;

    (void)cancel;
    if (arguments != NULL && arguments[0] != '\0') {
        items = read_session_file(arguments, error);
        if (items == NULL) {
            return false;
        }
        result = replace_messages(chat, items, error);
        telos_value_release(items);
        if (!result || !persist_session_snapshot(chat, error) ||
            !checkpoint_messages(chat, error)) {
            return false;
        }
        return emit_notice(emit, emit_context, "session resumed", error);
    }
    if (chat->checkpoint_count == 0) {
        return emit_notice(emit, emit_context, "no session checkpoint exists",
                           error);
    }
    restore_checkpoint(chat);
    return emit_notice(emit, emit_context, "session checkpoint resumed", error);
}

static bool copy_command(const char *arguments,
                         const struct telos_cancel *cancel,
                         telos_frontend_emit_fn emit,
                         void *emit_context,
                         void *context,
                         struct telos_error **error)
{
    struct chat_session *chat = context;

    (void)arguments;
    (void)cancel;
    for (size_t index = chat->message_count; index > 0; --index) {
        const struct telos_value *message = chat->messages[index - 1];
        const char *role = telos_value_string(telos_value_get(message, "role"));
        const char *content =
            telos_value_string(telos_value_get(message, "content"));

        if (role != NULL && strcmp(role, "assistant") == 0 && content != NULL) {
            return emit_frontend(
                &(struct observer_context){
                    .emit = emit,
                    .emit_context = emit_context,
                },
                TELOS_FRONTEND_CLIPBOARD, content, "clipboard", error);
        }
    }
    return emit_notice(emit, emit_context, "no assistant response to copy",
                       error);
}

static bool export_command(const char *arguments,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *context,
                           struct telos_error **error)
{
    struct chat_session *chat = context;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Usage: /export PATH");
        return false;
    }
    if (!write_session_file(chat, arguments, error)) {
        return false;
    }
    return emit_notice(emit, emit_context, "session exported", error);
}

static bool import_command(const char *arguments,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *context,
                           struct telos_error **error)
{
    struct chat_session *chat = context;
    struct telos_value *items;
    bool result;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Usage: /import PATH");
        return false;
    }
    items = read_session_file(arguments, error);
    if (items == NULL) {
        return false;
    }
    result = replace_messages(chat, items, error);
    telos_value_release(items);
    if (!result || !persist_session_snapshot(chat, error)) {
        return false;
    }
    return emit_notice(emit, emit_context, "session imported", error);
}

static bool settings_command(const char *arguments,
                             const struct telos_cancel *cancel,
                             telos_frontend_emit_fn emit,
                             void *emit_context,
                             void *context,
                             struct telos_error **error)
{
    struct chat_session *chat = context;
    char message[512];

    (void)arguments;
    (void)cancel;
    if (snprintf(message, sizeof(message),
                 "provider=%s model=%s thinking=%s endpoint=%s",
                 chat_provider_get(chat), chat_model_get(chat),
                 chat->thinking_level, chat->configured_endpoint) >=
        (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Settings summary is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool replace_provider_options(struct chat_session *chat,
                                     const char *key,
                                     const struct telos_value *value,
                                     struct telos_error **error)
{
    struct telos_value *options;

    if (key != NULL && value == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Provider thinking option allocation failed");
        return false;
    }
    if (key == NULL) {
        options = telos_value_new_object(NULL, NULL, 0);
    } else {
        const char *keys[] = {key};
        const struct telos_value *values[] = {value};

        options = telos_value_new_object(keys, values, 1);
    }
    if (options == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Provider thinking options allocation failed");
        return false;
    }
    telos_value_release(chat->provider_options);
    chat->provider_options = options;
    return true;
}

static bool thinking_level_valid(const char *level)
{
    static const char *const levels[] = {
        "off", "minimal", "low", "medium", "high", "xhigh", "max",
    };

    for (size_t index = 0; index < sizeof(levels) / sizeof(levels[0]);
         ++index) {
        if (strcmp(level, levels[index]) == 0) {
            return true;
        }
    }
    return false;
}

static int64_t thinking_budget(const char *level)
{
    if (strcmp(level, "minimal") == 0) {
        return 1024;
    }
    if (strcmp(level, "low") == 0) {
        return 2048;
    }
    if (strcmp(level, "medium") == 0) {
        return 4096;
    }
    if (strcmp(level, "high") == 0) {
        return 8192;
    }
    if (strcmp(level, "xhigh") == 0) {
        return 16384;
    }
    return 32768;
}

static bool set_thinking_options(struct chat_session *chat,
                                 const char *level,
                                 struct telos_error **error)
{
    const struct telos_model_descriptor *model = chat->selected_model;
    struct telos_value *value = NULL;
    bool result;

    if (strcmp(level, "off") == 0) {
        return replace_provider_options(chat, NULL, NULL, error);
    }
    if (model == NULL ||
        (model->capabilities & TELOS_MODEL_CAPABILITY_REASONING) == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                  "The selected model does not support thinking levels");
        return false;
    }
    if (model->api == TELOS_MODEL_API_OPENAI_RESPONSES) {
        const char *keys[] = {"effort"};
        struct telos_value *effort = telos_value_new_string(level);
        const struct telos_value *values[] = {effort};

        value = effort == NULL ? NULL : telos_value_new_object(keys, values, 1);
        telos_value_release(effort);
        result = replace_provider_options(chat, "reasoning", value, error);
        telos_value_release(value);
        return result;
    }
    if (model->api == TELOS_MODEL_API_ANTHROPIC_MESSAGES) {
        const char *keys[] = {"type", "budget_tokens"};
        struct telos_value *type = telos_value_new_string("enabled");
        struct telos_value *budget = telos_value_new_integer(
            thinking_budget(level));
        const struct telos_value *values[] = {type, budget};

        value = type == NULL || budget == NULL
                    ? NULL
                    : telos_value_new_object(keys, values, 2);
        telos_value_release(budget);
        telos_value_release(type);
        result = replace_provider_options(chat, "thinking", value, error);
        telos_value_release(value);
        return result;
    }
    {
        const char *keys[] = {"type"};
        struct telos_value *type = telos_value_new_string("enabled");
        const struct telos_value *values[] = {type};

        value = type == NULL ? NULL : telos_value_new_object(keys, values, 1);
        telos_value_release(type);
        result = replace_provider_options(chat, "thinking", value, error);
        telos_value_release(value);
        return result;
    }
}

static bool thinking_command(const char *arguments,
                             const struct telos_cancel *cancel,
                             telos_frontend_emit_fn emit,
                             void *emit_context,
                             void *context,
                             struct telos_error **error)
{
    struct chat_session *chat = context;
    char level[CHAT_THINKING_LEVEL_SIZE];
    char message[128];

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        if (snprintf(message, sizeof(message), "thinking=%s",
                     chat->thinking_level) >= (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Thinking status is too long");
            return false;
        }
        return emit_notice(emit, emit_context, message, error);
    }
    if (!copy_trimmed(arguments, level, sizeof(level), error) ||
        !thinking_level_valid(level)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Thinking level must be off, minimal, low, medium, high, "
                  "xhigh, or max");
        return false;
    }
    if (!set_thinking_options(chat, level, error)) {
        return false;
    }
    memset(chat->thinking_level, 0, sizeof(chat->thinking_level));
    memcpy(chat->thinking_level, level, strlen(level) + 1);
    if (snprintf(message, sizeof(message), "Thinking level set to %s", level) >=
        (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Thinking status is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool reload_command(const char *arguments,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *context,
                           struct telos_error **error)
{
    struct chat_session *chat = context;
    struct telos_prompt_snapshot *prompt;

    (void)arguments;
    (void)cancel;
    prompt = chat->prompt;
    chat->prompt = NULL;
    if (!create_prompt(chat, chat->home_directory, chat->current_directory,
                       error)) {
        chat->prompt = prompt;
        return false;
    }
    telos_prompt_snapshot_release(prompt);
    return emit_notice(emit, emit_context, "runtime guidance reloaded", error);
}

static bool trust_command(const char *arguments,
                          const struct telos_cancel *cancel,
                          telos_frontend_emit_fn emit,
                          void *emit_context,
                          void *context,
                          struct telos_error **error)
{
    struct chat_session *chat = context;
    char root[CHAT_PATH_SIZE];
    char message[CHAT_PATH_SIZE + 32];

    (void)arguments;
    (void)cancel;
    if (!find_project_root(chat->current_directory, root)) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Trusted project root could not be resolved");
        return false;
    }
    if (snprintf(message, sizeof(message), "trusted project root: %s", root) >=
        (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Trusted project root is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool static_notice_command(const char *arguments,
                                  const struct telos_cancel *cancel,
                                  telos_frontend_emit_fn emit,
                                  void *emit_context,
                                  void *context,
                                  struct telos_error **error)
{
    const char *message = context;

    (void)arguments;
    (void)cancel;
    return emit_notice(emit, emit_context, message, error);
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
    char spec[TELOS_COMMAND_ARGUMENT_SIZE];
    char message[TELOS_COMMAND_ARGUMENT_SIZE];
    const struct telos_model_descriptor *model;
    struct telos_error *selection_error = NULL;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0' ||
        !copy_trimmed(arguments, spec, sizeof(spec), error) ||
        spec[0] == '\0') {
        if (arguments != NULL && arguments[0] != '\0' &&
            error != NULL && *error != NULL) {
            return false;
        }
        if (chat->selected_model != NULL) {
            list.provider_filter = chat->selected_model->provider;
        } else if (chat->configured_provider != NULL &&
                   strcmp(chat->configured_provider, "unconfigured") != 0) {
            list.provider_filter = chat->configured_provider;
        } else {
            list.provider_filter = NULL;
        }
        if (!telos_model_catalog_visit(&chat->model_catalog,
                                       append_model_list, &list, error)) {
            return false;
        }
        if (list.used == 0) {
            const char *message =
                list.provider_filter == NULL
                    ? "No models are configured"
                    : "No models are configured for the current provider";

            memcpy(list.text, message, strlen(message) + 1);
        }
        return emit_frontend(&observer, TELOS_FRONTEND_NOTICE, list.text,
                             NULL, error);
    }
    if (!telos_model_catalog_select_spec(&chat->model_catalog, spec,
                                         &selection_error)) {
        const char *provider = chat->selected_model == NULL
                                   ? chat->configured_provider
                                   : chat->selected_model->provider;
        const char *model_id = spec;
        char provider_name[64];
        char *separator = strchr(spec, '/');
        struct telos_model_descriptor custom;

        if (selection_error == NULL ||
            telos_error_code(selection_error) != ENOENT) {
            if (error != NULL && *error == NULL) {
                *error = selection_error;
                selection_error = NULL;
            }
            telos_error_release(selection_error);
            return false;
        }
        telos_error_release(selection_error);
        selection_error = NULL;
        if (separator != NULL) {
            size_t provider_size = (size_t)(separator - spec);

            if (provider_size == 0 || provider_size >= sizeof(provider_name) ||
                separator[1] == '\0') {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Model selection is invalid");
                return false;
            }
            memcpy(provider_name, spec, provider_size);
            provider_name[provider_size] = '\0';
            provider = canonical_provider(provider_name);
            model_id = separator + 1;
        }
        if (provider == NULL || provider[0] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "A Provider is required for a custom model");
            return false;
        }
        provider = canonical_provider(provider);
        if (strcmp(provider, "openai") != 0 &&
            strcmp(provider, "deepseek") != 0 &&
            strcmp(provider, "zai") != 0 &&
            strcmp(provider, "anthropic") != 0) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                      "The selected Provider Plugin is not available");
            return false;
        }
        if (strlen(model_id) >= CHAT_MODEL_ID_SIZE ||
            chat->model_catalog.count >= TELOS_MODEL_CATALOG_CAPACITY) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Custom model identifier is too long");
            return false;
        }
        memcpy(chat->model_storage[chat->model_catalog.count], model_id,
               strlen(model_id) + 1);
        custom = (struct telos_model_descriptor){
            .provider = provider,
            .id = chat->model_storage[chat->model_catalog.count],
            .name = chat->model_storage[chat->model_catalog.count],
            .api = model_api_for_provider(provider),
            .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                            TELOS_MODEL_CAPABILITY_TOOLS,
        };
        if (!telos_model_catalog_add(&chat->model_catalog, &custom, error) ||
            !telos_model_catalog_select(&chat->model_catalog, provider,
                                        model_id, error)) {
            return false;
        }
    }
    model = telos_model_catalog_current(&chat->model_catalog);
    if (model == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                  "Selected model is unavailable");
        return false;
    }
    chat->selected_model = model;
    chat->model = model->variant_of == NULL ? model->id
                                            : model->variant_of;
    chat->configured_provider = model->provider;
    if ((model->capabilities & TELOS_MODEL_CAPABILITY_REASONING) == 0) {
        if (!set_thinking_options(chat, "off", error)) {
            return false;
        }
        memcpy(chat->thinking_level, "off", sizeof("off"));
    } else {
        const char *level = model->reasoning == NULL ? chat->thinking_level
                                                     : model->reasoning;

        if (!set_thinking_options(chat, level, error)) {
            return false;
        }
        memcpy(chat->thinking_level, level, strlen(level) + 1);
    }
    if (!ensure_authentication(chat, model->provider, error) ||
        !create_provider(chat, error)) {
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

static bool scoped_models_command(const char *arguments,
                                  const struct telos_cancel *cancel,
                                  telos_frontend_emit_fn emit,
                                  void *emit_context,
                                  void *context,
                                  struct telos_error **error)
{
    return model_command(arguments, cancel, emit, emit_context, context, error);
}

static bool login_command_handler(const char *arguments,
                                  const struct telos_cancel *cancel,
                                  telos_frontend_emit_fn emit,
                                  void *emit_context, void *context,
                                  struct telos_error **error)
{
    struct chat_session *chat = context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
        .provider = chat_provider_get(chat),
    };
    const struct telos_authentication_definition_v1 *saved_definition =
        chat->authentication_definition;
    struct telos_authentication *saved_authentication = chat->authentication;
    char provider[64];
    bool targeted = arguments != NULL && arguments[0] != '\0' &&
                    strcmp(arguments, "status") != 0;
    bool result;

    if (arguments != NULL && strcmp(arguments, "status") == 0) {
        return login_status_command(chat, &observer, error);
    }
    if (targeted) {
        if (!copy_trimmed(arguments, provider, sizeof(provider), error) ||
            !ensure_authentication(chat, canonical_provider(provider), error)) {
            chat->authentication_definition = saved_definition;
            chat->authentication = saved_authentication;
            return false;
        }
        observer.provider = canonical_provider(provider);
    }
    result = login_command(chat, cancel, &observer, error);
    if (targeted) {
        chat->authentication_definition = saved_definition;
        chat->authentication = saved_authentication;
    }
    return result;
}

static bool logout_command_handler(const char *arguments,
                                   const struct telos_cancel *cancel,
                                   telos_frontend_emit_fn emit,
                                   void *emit_context, void *context,
                                   struct telos_error **error)
{
    struct chat_session *chat = context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
        .provider = chat_provider_get(chat),
    };
    const struct telos_authentication_definition_v1 *saved_definition =
        chat->authentication_definition;
    struct telos_authentication *saved_authentication = chat->authentication;
    char provider[64];
    bool targeted = arguments != NULL && arguments[0] != '\0';
    bool result;

    (void)cancel;
    if (targeted) {
        if (!copy_trimmed(arguments, provider, sizeof(provider), error) ||
            !ensure_authentication(chat, canonical_provider(provider), error)) {
            chat->authentication_definition = saved_definition;
            chat->authentication = saved_authentication;
            return false;
        }
        observer.provider = canonical_provider(provider);
    }
    result = logout_command(chat, &observer, error);
    if (targeted) {
        chat->authentication_definition = saved_definition;
        chat->authentication = saved_authentication;
    }
    return result;
}

static bool login_status_command_handler(const char *arguments,
                                         const struct telos_cancel *cancel,
                                         telos_frontend_emit_fn emit,
                                         void *emit_context, void *context,
                                         struct telos_error **error)
{
    struct chat_session *chat = context;
    struct observer_context observer = {
        .emit = emit,
        .emit_context = emit_context,
        .provider = chat_provider_get(chat),
    };
    const struct telos_authentication_definition_v1 *saved_definition =
        chat->authentication_definition;
    struct telos_authentication *saved_authentication = chat->authentication;
    char provider[64];
    bool targeted = arguments != NULL && arguments[0] != '\0';
    bool result;

    (void)cancel;
    if (targeted) {
        if (!copy_trimmed(arguments, provider, sizeof(provider), error) ||
            !ensure_authentication(chat, canonical_provider(provider), error)) {
            chat->authentication_definition = saved_definition;
            chat->authentication = saved_authentication;
            return false;
        }
        observer.provider = canonical_provider(provider);
    }
    result = login_status_command(chat, &observer, error);
    if (targeted) {
        chat->authentication_definition = saved_definition;
        chat->authentication = saved_authentication;
    }
    return result;
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
            .name = "new",
            .help = "start a new conversation",
            .run = new_command,
            .context = chat,
        },
        {
            .name = "model",
            .help = "list or select a model",
            .run = model_command,
            .context = chat,
        },
        {
            .name = "thinking",
            .help = "show or set the thinking level",
            .run = thinking_command,
            .context = chat,
        },
        {
            .name = "scoped-models",
            .help = "list models available to this session",
            .run = scoped_models_command,
            .context = chat,
        },
        {
            .name = "name",
            .help = "show or set the session name",
            .run = name_command,
            .context = chat,
        },
        {
            .name = "session",
            .help = "show the current session summary",
            .run = session_command,
            .context = chat,
        },
        {
            .name = "sessions",
            .help = "list persisted sessions",
            .run = sessions_command,
            .context = chat,
        },
        {
            .name = "tree",
            .help = "show the conversation tree",
            .run = tree_command,
            .context = chat,
        },
        {
            .name = "compact",
            .help = "compact older conversation messages",
            .run = compact_command,
            .context = chat,
        },
        {
            .name = "fork",
            .help = "save a session fork checkpoint",
            .run = fork_command,
            .context = chat,
        },
        {
            .name = "clone",
            .help = "clone a session fork checkpoint",
            .run = fork_command,
            .context = chat,
        },
        {
            .name = "resume",
            .help = "resume a checkpoint or exported session file",
            .run = resume_command,
            .context = chat,
        },
        {
            .name = "copy",
            .help = "show the latest assistant response",
            .run = copy_command,
            .context = chat,
        },
        {
            .name = "export",
            .help = "export the session as a JSON array",
            .run = export_command,
            .context = chat,
        },
        {
            .name = "import",
            .help = "import a JSON session array",
            .run = import_command,
            .context = chat,
        },
        {
            .name = "settings",
            .help = "show current provider settings",
            .run = settings_command,
            .context = chat,
        },
        {
            .name = "reload",
            .help = "reload runtime guidance",
            .run = reload_command,
            .context = chat,
        },
        {
            .name = "hotkeys",
            .help = "show terminal editing keys",
            .run = static_notice_command,
            .context =
                "Enter submits · Ctrl+J or Alt+Enter adds a line · Esc cancels · "
                "Ctrl+G opens $EDITOR",
        },
        {
            .name = "changelog",
            .help = "show the Telos release note",
            .run = static_notice_command,
            .context =
                "Telos 0.1.0: Plugin-backed Pi-compatible terminal agent",
        },
        {
            .name = "trust",
            .help = "show project trust policy",
            .run = trust_command,
            .context = chat,
        },
        {
            .name = "share",
            .help = "export the session for sharing",
            .run = export_command,
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
    const char *provider = canonical_provider(provider_name);

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
                       : (strcmp(provider, "anthropic") == 0
                              ? TELOS_MODEL_API_ANTHROPIC_MESSAGES
                              : TELOS_MODEL_API_OPENAI_CHAT),
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
    const char *thinking = telos_config_get(config, "agent.thinking");
    const char *authentication_endpoint =
        getenv("TELOS_OPENAI_AUTH_ENDPOINT");

    chat->home_directory = home_directory;
    chat->current_directory = current_directory;
    if (thinking == NULL || !thinking_level_valid(thinking) ||
        strlen(thinking) >= sizeof(chat->thinking_level)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent thinking level is invalid");
        return false;
    }
    memcpy(chat->thinking_level, thinking, strlen(thinking) + 1);
    if (!initialize_session_store(chat, home_directory, error)) {
        return false;
    }
    if (provider_name == NULL ||
        (strcmp(canonical_provider(provider_name), "openai") != 0 &&
         strcmp(canonical_provider(provider_name), "deepseek") != 0 &&
         strcmp(canonical_provider(provider_name), "zai") != 0 &&
         strcmp(canonical_provider(provider_name), "anthropic") != 0)) {
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
    if (snprintf(chat->authentication_directory,
                 sizeof(chat->authentication_directory), "%s/.telos",
                 home_directory) >=
        (int)sizeof(chat->authentication_directory)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Agent authentication state path is too long");
        return false;
    }
    chat->authentication_endpoint = authentication_endpoint;
    if (!ensure_authentication(chat, chat->configured_provider, error)) {
        return false;
    }
    chat->secret_broker = telos_secret_broker_create(resolve_secret, chat,
                                                      error);
    if (chat->secret_broker == NULL) {
        return false;
    }
    if (chat->model != NULL && !create_provider(chat, error)) {
        return false;
    }
    {
        static const char *const tool_capabilities[] = {
            "filesystem.read",
            "filesystem.write",
            "process.spawn",
        };
        const struct telos_posix_tools_config tools_config = {
            .working_directory = current_directory,
            .shell = getenv("TELOS_AGENT_SHELL"),
        };

        chat->registry = telos_registry_create(tool_capabilities, 3, error);
        if (chat->registry == NULL) {
            return false;
        }
        chat->posix_tools = telos_posix_tools_create(&tools_config, error);
        if (chat->posix_tools == NULL ||
            !telos_posix_tools_register(chat->posix_tools, chat->registry,
                                        error)) {
            return false;
        }
        chat->capability_broker = telos_capability_broker_create(
            tool_capabilities, 3, allow_tools, chat, error);
        if (chat->capability_broker == NULL) {
            return false;
        }
        chat->tools = telos_posix_tools_describe(error);
    }
    if (chat->registry == NULL) {
        return false;
    }
    chat->generation = telos_registry_acquire(chat->registry);
    if (chat->generation == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent Registry Snapshot allocation failed");
        return false;
    }
    chat->provider_options = telos_value_new_object(NULL, NULL, 0);
    if (chat->tools == NULL || chat->provider_options == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent Provider options allocation failed");
        return false;
    }
    if (chat->selected_model != NULL &&
        !set_thinking_options(chat, chat->thinking_level, error)) {
        return false;
    }
    return create_prompt(chat, home_directory, current_directory, error);
}

static void clear_chat(struct chat_session *chat)
{
    clear_messages(chat);
    clear_checkpoint(chat);
    telos_value_release(chat->provider_options);
    telos_value_release(chat->tools);
    telos_prompt_snapshot_release(chat->prompt);
    telos_capability_broker_destroy(chat->capability_broker);
    telos_registry_generation_release(chat->generation);
    telos_registry_destroy(chat->registry);
    telos_posix_tools_destroy(chat->posix_tools);
    if (chat->provider_destroy != NULL) {
        chat->provider_destroy(chat->provider_context);
    }
    telos_event_store_destroy(chat->session_store);
    telos_secret_broker_destroy(chat->secret_broker);
    for (size_t index = 0; index < chat->authentication_count; ++index) {
        const struct chat_authentication_slot *slot =
            &chat->authentications[index];

        slot->definition->destroy(slot->authentication);
    }
}

bool telos_chat_run(const struct telos_config *config,
                    const char *home_directory,
                    const char *current_directory,
                    const char *initial_prompt,
                    bool single_turn,
                    bool json_output,
                    bool rpc_mode,
                    bool continue_session,
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
    if (continue_session && !load_latest_session(&chat, error)) {
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
            .json_output = json_output,
            .rpc_mode = rpc_mode,
        };

        result = telos_terminal_frontend_run(&frontend, error);
    }
    clear_chat(&chat);
    return result;
}
