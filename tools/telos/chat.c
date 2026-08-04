#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
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
#include <telos/plugins/tui_frontend.h>
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
#define CHAT_SESSION_PREVIEW_SIZE 192U
#define CHAT_SESSION_COMPLETION_CAPACITY 64U
#define CHAT_SESSION_FILE_NAME_SIZE 256U
#define CHAT_SESSION_DIRECTORY_SIZE CHAT_PATH_SIZE
#define CHAT_THINKING_LEVEL_SIZE 16U
#define CHAT_MODEL_ID_SIZE 256U
#define CHAT_STATUS_SPEC_SIZE 128U
#define CHAT_GIT_BRANCH_SIZE 128U
#define CHAT_AUTHENTICATION_CAPACITY 4U
#define CHAT_OPENAI_CONTEXT_WINDOW 258000U
#define CHAT_DEEPSEEK_CONTEXT_WINDOW 128000U
#define CHAT_ANTHROPIC_CONTEXT_WINDOW 200000U

struct chat_authentication_slot {
    const char *provider;
    const struct telos_authentication_definition_v1 *definition;
    struct telos_authentication *authentication;
};

struct chat_saved_session {
    char file_name[CHAT_SESSION_FILE_NAME_SIZE];
    char identifier[TELOS_ID_TEXT_SIZE];
    char name[CHAT_SESSION_NAME_SIZE];
    char preview[CHAT_SESSION_PREVIEW_SIZE];
    time_t modified;
    bool current;
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
    const struct telos_config *config;
    const char *home_directory;
    const char *current_directory;
    char authentication_directory[CHAT_PATH_SIZE];
    const char *authentication_endpoint;
    struct chat_authentication_slot authentications[
        CHAT_AUTHENTICATION_CAPACITY];
    size_t authentication_count;
    char thinking_level[CHAT_THINKING_LEVEL_SIZE];
    char status_spec[CHAT_STATUS_SPEC_SIZE];
    char git_branch[CHAT_GIT_BRANCH_SIZE];
    struct telos_frontend_status frontend_status;
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
static struct telos_value *read_session_file(const char *path,
                                             char *session_name,
                                             size_t session_name_size,
                                             struct telos_error **error);
static struct telos_value *read_jsonl_session_file(const char *path,
                                                   char *session_name,
                                                   size_t session_name_size,
                                                   struct telos_error **error);
static bool persist_session_marker(struct chat_session *chat,
                                   const char *type,
                                   struct telos_error **error);
static bool persist_session_name(struct chat_session *chat,
                                 struct telos_error **error);
static bool set_session_name(struct chat_session *chat, const char *name,
                             bool persist, struct telos_error **error);
static void infer_session_name_from_items(const struct telos_value *items,
                                          char target[], size_t target_size);
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

static void infer_session_name_from_text(const char *text, char target[],
                                         size_t target_size)
{
    const char *start = text;
    size_t size;
    size_t limit;

    if (target == NULL || target_size == 0) {
        return;
    }
    target[0] = '\0';
    if (text == NULL) {
        return;
    }
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    size = strcspn(start, "\r\n");
    while (size > 0 && isspace((unsigned char)start[size - 1])) {
        --size;
    }
    while (size > 0 &&
           (start[size - 1] == '.' || start[size - 1] == '!' ||
            start[size - 1] == '?' || start[size - 1] == ':' ||
            start[size - 1] == ';' || start[size - 1] == ',')) {
        --size;
    }
    if (size == 0) {
        return;
    }
    if (size < target_size) {
        memcpy(target, start, size);
        target[size] = '\0';
        return;
    }
    limit = target_size > sizeof("...") ? target_size - sizeof("...") : 0;
    while (limit > 0 && (start[limit] & 0xc0) == 0x80) {
        --limit;
    }
    if (limit == 0) {
        return;
    }
    memcpy(target, start, limit);
    memcpy(target + limit, "...", sizeof("..."));
}

static void infer_session_name_from_items(const struct telos_value *items,
                                          char target[], size_t target_size)
{
    if (target == NULL || target_size == 0) {
        return;
    }
    target[0] = '\0';
    if (items == NULL || telos_value_type(items) != TELOS_VALUE_ARRAY) {
        return;
    }
    for (size_t index = 0; index < telos_value_count(items); ++index) {
        const struct telos_value *message = telos_value_at(items, index);
        const char *role = telos_value_string(telos_value_get(message, "role"));
        const char *content =
            telos_value_string(telos_value_get(message, "content"));

        if (role != NULL && strcmp(role, "user") == 0 && content != NULL) {
            infer_session_name_from_text(content, target, target_size);
            if (target[0] != '\0') {
                return;
            }
        }
    }
}

static bool persist_session_name(struct chat_session *chat,
                                 struct telos_error **error)
{
    const char *keys[] = {"name"};
    struct telos_value *name = NULL;
    struct telos_value *payload = NULL;
    const struct telos_value *values[1];
    bool result;

    if (chat->session_name[0] == '\0') {
        return true;
    }
    name = telos_value_new_string(chat->session_name);
    values[0] = name;
    payload = name == NULL ? NULL : telos_value_new_object(keys, values, 1);
    if (payload == NULL) {
        telos_value_release(name);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session name allocation failed");
        return false;
    }
    result = persist_session_value(chat, "session.name", payload, error);
    telos_value_release(payload);
    telos_value_release(name);
    return result;
}

static bool set_session_name(struct chat_session *chat, const char *name,
                             bool persist, struct telos_error **error)
{
    char candidate[CHAT_SESSION_NAME_SIZE];
    char previous[CHAT_SESSION_NAME_SIZE];
    const char *start = name == NULL ? "" : name;
    size_t size;

    while (isspace((unsigned char)*start)) {
        ++start;
    }
    size = strlen(start);
    while (size > 0 && isspace((unsigned char)start[size - 1])) {
        --size;
    }
    if (size >= sizeof(candidate)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Session name is too long");
        return false;
    }
    memcpy(candidate, start, size);
    candidate[size] = '\0';
    memcpy(previous, chat->session_name, sizeof(previous));
    memcpy(chat->session_name, candidate, size + 1);
    if (persist && !persist_session_name(chat, error)) {
        memcpy(chat->session_name, previous, sizeof(chat->session_name));
        return false;
    }
    return true;
}

static bool ensure_session_name(struct chat_session *chat,
                                const struct telos_value *items,
                                const char *input,
                                bool persist,
                                struct telos_error **error)
{
    char inferred[CHAT_SESSION_NAME_SIZE];

    if (chat->session_name[0] != '\0') {
        return true;
    }
    if (items != NULL) {
        infer_session_name_from_items(items, inferred, sizeof(inferred));
    } else {
        infer_session_name_from_text(input, inferred, sizeof(inferred));
    }
    return inferred[0] == '\0' ||
           set_session_name(chat, inferred, persist, error);
}

static bool persist_session_snapshot(struct chat_session *chat,
                                     struct telos_error **error)
{
    if (!persist_session_marker(chat, "session.reset", error)) {
        return false;
    }
    if (!persist_session_name(chat, error)) {
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

static const char *chat_thinking_get(void *context)
{
    const struct chat_session *chat = context;

    return chat->thinking_level;
}

static const char *chat_branch_get(void *context)
{
    const struct chat_session *chat = context;

    return chat->git_branch;
}

static size_t chat_context_used_get(void *context)
{
    const struct chat_session *chat = context;

    return chat->conversation_bytes > SIZE_MAX - 3
               ? SIZE_MAX / 4
               : (chat->conversation_bytes + 3) / 4;
}

static size_t chat_context_window_get(void *context)
{
    const struct chat_session *chat = context;

    if (chat->selected_model != NULL &&
        chat->selected_model->context_window > 0) {
        return chat->selected_model->context_window;
    }
    if (chat->selected_model == NULL ||
        chat->selected_model->provider == NULL) {
        return 0;
    }
    if (strcmp(chat->selected_model->provider, "openai") == 0) {
        return CHAT_OPENAI_CONTEXT_WINDOW;
    }
    if (strcmp(chat->selected_model->provider, "deepseek") == 0 ||
        strcmp(chat->selected_model->provider, "zai") == 0) {
        return CHAT_DEEPSEEK_CONTEXT_WINDOW;
    }
    if (strcmp(chat->selected_model->provider, "anthropic") == 0) {
        return CHAT_ANTHROPIC_CONTEXT_WINDOW;
    }
    return 0;
}

static bool discover_git_branch(const char *directory, char target[],
                                size_t target_size)
{
    int descriptors[2];
    pid_t child;
    int status = 0;
    size_t used = 0;
    pid_t waited;

    if (target == NULL || target_size == 0) {
        return false;
    }
    target[0] = '\0';
    if (directory == NULL || pipe(descriptors) != 0) {
        return false;
    }
    child = fork();
    if (child < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return false;
    }
    if (child == 0) {
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(descriptors[0]);
        close(descriptors[1]);
        execlp("git", "git", "-C", directory, "rev-parse", "--abbrev-ref",
               "HEAD", (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    while (used + 1 < target_size) {
        ssize_t count = read(descriptors[0], target + used,
                             target_size - used - 1);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        used += (size_t)count;
        if (memchr(target, '\n', used) != NULL) {
            break;
        }
    }
    close(descriptors[0]);
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        target[0] = '\0';
        return false;
    }
    target[used] = '\0';
    for (size_t index = 0; index < used; ++index) {
        if (target[index] == '\n' || target[index] == '\r') {
            target[index] = '\0';
            break;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
           target[0] != '\0' && strcmp(target, "HEAD") != 0;
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

static const char *model_provider_for(const char *provider)
{
    const char *canonical = canonical_provider(provider);

    if (canonical == NULL) {
        return NULL;
    }
    if (strcmp(canonical, "openai") == 0) {
        return "openai";
    }
    if (strcmp(canonical, "deepseek") == 0) {
        return "deepseek";
    }
    if (strcmp(canonical, "zai") == 0) {
        return "zai";
    }
    if (strcmp(canonical, "anthropic") == 0) {
        return "anthropic";
    }
    return NULL;
}

static bool model_provider_supported(const char *provider)
{
    return model_provider_for(provider) != NULL;
}

static const char *migrate_model_id(const char *provider, const char *model)
{
    if (model == NULL || provider == NULL || strcmp(provider, "openai") != 0) {
        return model;
    }
    if (strcmp(model, "gpt-5") == 0) {
        return "gpt-5.5";
    }
    if (strcmp(model, "gpt-5/sol") == 0) {
        return "gpt-5.6-sol";
    }
    if (strcmp(model, "gpt-5/luna") == 0) {
        return "gpt-5.6-luna";
    }
    if (strcmp(model, "gpt-5/terra") == 0) {
        return "gpt-5.6-terra";
    }
    return model;
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

static bool status_field_token(const char *token, size_t size,
                               unsigned int *field)
{
    static const struct {
        const char *name;
        unsigned int field;
    } fields[] = {
        {"model", TELOS_FRONTEND_STATUS_MODEL},
        {"thinking", TELOS_FRONTEND_STATUS_THINKING},
        {"path", TELOS_FRONTEND_STATUS_PATH},
        {"branch", TELOS_FRONTEND_STATUS_BRANCH},
        {"context", TELOS_FRONTEND_STATUS_CONTEXT},
    };

    for (size_t index = 0; index < sizeof(fields) / sizeof(fields[0]);
         ++index) {
        if (strlen(fields[index].name) == size &&
            strncmp(fields[index].name, token, size) == 0) {
            *field = fields[index].field;
            return true;
        }
    }
    return false;
}

static bool format_status_spec(unsigned int fields, char *target,
                               size_t target_size,
                               struct telos_error **error)
{
    static const struct {
        const char *name;
        unsigned int field;
    } names[] = {
        {"model", TELOS_FRONTEND_STATUS_MODEL},
        {"thinking", TELOS_FRONTEND_STATUS_THINKING},
        {"path", TELOS_FRONTEND_STATUS_PATH},
        {"branch", TELOS_FRONTEND_STATUS_BRANCH},
        {"context", TELOS_FRONTEND_STATUS_CONTEXT},
    };
    size_t used = 0;

    if (fields == 0) {
        if (snprintf(target, target_size, "none") >= (int)target_size) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Status specification is too long");
            return false;
        }
        return true;
    }
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]);
         ++index) {
        if ((fields & names[index].field) == 0) {
            continue;
        }
        int written = snprintf(target + used, target_size - used, "%s%s",
                               used == 0 ? "" : ",", names[index].name);

        if (written < 0 || (size_t)written >= target_size - used) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Status specification is too long");
            return false;
        }
        used += (size_t)written;
    }
    return true;
}

static bool parse_status_spec(const char *source, unsigned int *fields,
                              char *canonical, size_t canonical_size,
                              struct telos_error **error)
{
    static const unsigned int all_fields =
        TELOS_FRONTEND_STATUS_MODEL | TELOS_FRONTEND_STATUS_THINKING |
        TELOS_FRONTEND_STATUS_PATH | TELOS_FRONTEND_STATUS_BRANCH |
        TELOS_FRONTEND_STATUS_CONTEXT;
    char specification[CHAT_STATUS_SPEC_SIZE];
    unsigned int parsed = 0;
    bool special = false;
    bool token_seen = false;
    char *cursor;

    if (!copy_trimmed(source, specification, sizeof(specification), error) ||
        specification[0] == '\0') {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Status specification is empty");
        }
        return false;
    }
    cursor = specification;
    while (*cursor != '\0') {
        char *start;
        size_t size;
        unsigned int field;

        while (*cursor == ',' || isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ',' &&
               !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        size = (size_t)(cursor - start);
        token_seen = true;
        if ((size == sizeof("all") - 1 &&
             strncmp(start, "all", size) == 0) ||
            (size == sizeof("none") - 1 &&
             strncmp(start, "none", size) == 0)) {
            if (special || parsed != 0) {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Status all/none cannot be combined with fields");
                return false;
            }
            special = true;
            parsed = size == sizeof("all") - 1 ? all_fields : 0;
            continue;
        }
        if (special || !status_field_token(start, size, &field)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Status fields must be model, thinking, path, branch, "
                      "context, all, or none");
            return false;
        }
        parsed |= field;
    }
    if (!token_seen) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Status specification is empty");
        return false;
    }
    if (!format_status_spec(parsed, canonical, canonical_size, error)) {
        return false;
    }
    *fields = parsed;
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
                                             char *session_name,
                                             size_t session_name_size,
                                             struct telos_error **error)
{
    FILE *stream = fopen(path, "rb");
    long length;
    char *json;
    struct telos_value *items;

    if (session_name != NULL && session_name_size > 0) {
        session_name[0] = '\0';
    }

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
    if (items == NULL || telos_value_type(items) != TELOS_VALUE_ARRAY) {
        telos_value_release(items);
        if (error != NULL) {
            telos_error_release(*error);
            *error = NULL;
        }
        return read_jsonl_session_file(path, session_name, session_name_size,
                                       error);
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
                                                   char *session_name,
                                                   size_t session_name_size,
                                                   struct telos_error **error)
{
    struct telos_event_store *store = NULL;
    struct telos_value *items[CHAT_MAXIMUM_MESSAGES] = {0};
    struct telos_value *result = NULL;
    size_t count = 0;

    if (session_name != NULL && session_name_size > 0) {
        session_name[0] = '\0';
    }

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
            if (session_name != NULL && session_name_size > 0) {
                session_name[0] = '\0';
            }
        } else if (strcmp(type, "session.name") == 0) {
            const struct telos_value *payload = telos_event_payload(event);
            const char *name = payload == NULL
                                   ? NULL
                                   : telos_value_string(
                                         telos_value_get(payload, "name"));

            if (session_name != NULL && session_name_size > 0 && name != NULL &&
                strlen(name) < session_name_size) {
                memcpy(session_name, name, strlen(name) + 1);
            }
        } else if (strcmp(type, "message") == 0) {
            const struct telos_value *payload = telos_event_payload(event);
            const char *role = payload == NULL
                                   ? NULL
                                   : telos_value_string(
                                         telos_value_get(payload, "role"));
            const char *content = payload == NULL
                                      ? NULL
                                      : telos_value_string(
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

static bool append_message_value(struct chat_session *chat,
                                 struct telos_value *message, size_t size,
                                 struct telos_error **error)
{
    struct telos_value *owned_message;

    if (message == NULL || telos_value_type(message) != TELOS_VALUE_OBJECT) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent conversation message is invalid");
        return false;
    }
    if (size > CHAT_MAXIMUM_CONVERSATION_BYTES) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "Agent conversation message is too large");
        return false;
    }
    owned_message = telos_value_retain(message);
    if (owned_message == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Agent conversation message allocation failed");
        return false;
    }
    while (chat->message_count >= CHAT_MAXIMUM_MESSAGES ||
           size > CHAT_MAXIMUM_CONVERSATION_BYTES -
                      chat->conversation_bytes) {
        drop_messages(chat, chat->message_count >= 2 ? 2 : 1);
    }
    chat->messages[chat->message_count] = owned_message;
    chat->message_sizes[chat->message_count] = size;
    chat->message_count += 1;
    chat->conversation_bytes += size;
    if (!persist_session_value(chat, "message", owned_message, error)) {
        chat->message_count -= 1;
        chat->conversation_bytes -= size;
        chat->messages[chat->message_count] = NULL;
        chat->message_sizes[chat->message_count] = 0;
        telos_value_release(owned_message);
        return false;
    }
    return true;
}

static bool append_message(struct chat_session *chat, const char *role,
                           const char *text, struct telos_error **error)
{
    struct telos_value *message = create_message(role, text, error);
    bool result;

    if (message == NULL) {
        return false;
    }
    result = append_message_value(chat, message, strlen(text), error);
    telos_value_release(message);
    return result;
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

static void append_detail_text(char *target, size_t target_size, size_t *used,
                               const char *text, size_t maximum)
{
    size_t size = text == NULL ? 0 : strlen(text);

    if (target == NULL || used == NULL || target_size == 0 ||
        *used >= target_size) {
        return;
    }
    if (size > maximum) {
        size = maximum;
    }
    for (size_t index = 0; index < size && *used + 1 < target_size; ++index) {
        unsigned char value = (unsigned char)text[index];

        target[(*used)++] =
            value < 0x20U || value == 0x7fU ? ' ' : (char)value;
    }
    target[*used] = '\0';
    if (text != NULL && strlen(text) > maximum && *used + 4 < target_size) {
        memcpy(target + *used, "...", 4);
        *used += 3;
        target[*used] = '\0';
    }
}

static void append_tool_value(char *target, size_t target_size, size_t *used,
                              const struct telos_value *value)
{
    int64_t integer;
    bool boolean;
    const char *text;

    if (value == NULL) {
        return;
    }
    text = telos_value_string(value);
    if (text != NULL) {
        append_detail_text(target, target_size, used, text, 96);
        return;
    }
    if (telos_value_integer(value, &integer)) {
        int written = snprintf(target + *used, target_size - *used, "%lld",
                               (long long)integer);

        if (written > 0 && (size_t)written < target_size - *used) {
            *used += (size_t)written;
        }
        return;
    }
    if (telos_value_boolean(value, &boolean)) {
        append_detail_text(target, target_size, used,
                           boolean ? "true" : "false", SIZE_MAX);
        return;
    }
    append_detail_text(target, target_size, used,
                       telos_value_type(value) == TELOS_VALUE_ARRAY
                           ? "[list]"
                           : telos_value_type(value) == TELOS_VALUE_OBJECT
                                 ? "{...}"
                                 : "[value]",
                       SIZE_MAX);
}

static void append_tool_member(char *target, size_t target_size, size_t *used,
                               bool *has_member, const char *key,
                               const struct telos_value *value)
{
    if (value == NULL || *used + 1 >= target_size) {
        return;
    }
    if (*has_member) {
        append_detail_text(target, target_size, used, ", ", SIZE_MAX);
    }
    append_detail_text(target, target_size, used, key, SIZE_MAX);
    append_detail_text(target, target_size, used, "=", SIZE_MAX);
    append_tool_value(target, target_size, used, value);
    *has_member = true;
}

static void append_tool_arguments(char *target, size_t target_size,
                                  size_t *used,
                                  const struct telos_value *arguments)
{
    static const char *const preferred[] = {
        "path", "line_start", "line_end", "command", "query", "pattern",
        "url",
    };
    bool has_member = false;

    if (arguments == NULL) {
        return;
    }
    for (size_t index = 0; index < sizeof(preferred) / sizeof(preferred[0]);
         ++index) {
        append_tool_member(target, target_size, used, &has_member,
                           preferred[index],
                           telos_value_get(arguments, preferred[index]));
    }
    if (!has_member && telos_value_type(arguments) == TELOS_VALUE_OBJECT) {
        for (size_t index = 0;
             index < telos_value_count(arguments) && index < 2; ++index) {
            const char *key = telos_value_key_at(arguments, index);

            if (key != NULL && strcmp(key, "content") != 0) {
                append_tool_member(target, target_size, used, &has_member, key,
                                   telos_value_get(arguments, key));
            }
        }
    }
}

static void append_tool_result(char *target, size_t target_size, size_t *used,
                               const struct telos_value *result)
{
    const struct telos_value *value;
    int64_t integer;

    if (result == NULL || telos_value_type(result) != TELOS_VALUE_OBJECT) {
        return;
    }
    value = telos_value_get(result, "bytes");
    if (value != NULL && telos_value_integer(value, &integer)) {
        char summary[64];

        if (snprintf(summary, sizeof(summary), "wrote %lld bytes",
                     (long long)integer) < (int)sizeof(summary)) {
            append_detail_text(target, target_size, used, summary, SIZE_MAX);
        }
        return;
    }
    value = telos_value_get(result, "content");
    if (value != NULL && telos_value_string(value) != NULL) {
        char summary[64];

        if (snprintf(summary, sizeof(summary), "read %zu chars",
                     strlen(telos_value_string(value))) < (int)sizeof(summary)) {
            append_detail_text(target, target_size, used, summary, SIZE_MAX);
        }
        return;
    }
    value = telos_value_get(result, "output");
    if (value != NULL && telos_value_string(value) != NULL) {
        char summary[64];

        if (snprintf(summary, sizeof(summary), "output %zu chars",
                     strlen(telos_value_string(value))) < (int)sizeof(summary)) {
            append_detail_text(target, target_size, used, summary, SIZE_MAX);
        }
        return;
    }
    value = telos_value_get(result, "exit_code");
    if (value != NULL && telos_value_integer(value, &integer)) {
        char summary[64];

        if (snprintf(summary, sizeof(summary), "exit %lld",
                     (long long)integer) < (int)sizeof(summary)) {
            append_detail_text(target, target_size, used, summary, SIZE_MAX);
        }
    }
}

static void summarize_tool_event(const struct telos_agent_event *event,
                                 bool completed, char *target,
                                 size_t target_size)
{
    size_t used = 0;

    target[0] = '\0';
    append_tool_arguments(target, target_size, &used, event->tool_arguments);
    if (completed && event->tool_result != NULL) {
        if (used > 0) {
            append_detail_text(target, target_size, &used, " · ", SIZE_MAX);
        }
        append_tool_result(target, target_size, &used, event->tool_result);
    } else if (!completed && used == 0) {
        append_detail_text(target, target_size, &used, "running", SIZE_MAX);
    } else if (event->tool_error != NULL) {
        if (used > 0) {
            append_detail_text(target, target_size, &used, " · ", SIZE_MAX);
        }
        append_detail_text(target, target_size, &used, "error: ", SIZE_MAX);
        append_detail_text(target, target_size, &used,
                           telos_error_message(event->tool_error), 96);
    }
}

static bool observe_agent(const struct telos_agent_event *event,
                          void *context, struct telos_error **error)
{
    struct observer_context *observer = context;
    char detail[256];

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
        if (provider->kind == TELOS_PROVIDER_REASONING_ITEM) {
            return emit_frontend(observer, TELOS_FRONTEND_THINKING_DELTA,
                                 provider->delta, NULL, error);
        }
        return true;
    }
    if (event->kind == TELOS_AGENT_TOOL_STARTED) {
        summarize_tool_event(event, false, detail, sizeof(detail));
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_STARTED, detail,
                             event->tool_name, error);
    }
    if (event->kind == TELOS_AGENT_TOOL_COMPLETED) {
        summarize_tool_event(event, true, detail, sizeof(detail));
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_COMPLETED, detail,
                             event->tool_name, error);
    }
    if (event->kind == TELOS_AGENT_TOOL_FAILED) {
        summarize_tool_event(event, true, detail, sizeof(detail));
        return emit_frontend(observer, TELOS_FRONTEND_TOOL_FAILED, detail,
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
    if (!persist_session_name(chat, error)) {
        return false;
    }
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
    if (!set_session_name(chat, arguments, true, error)) {
        return false;
    }
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

static bool session_directory_path(const struct chat_session *chat,
                                   char directory[], size_t directory_size,
                                   struct telos_error **error)
{
    const char *separator = strrchr(chat->session_path, '/');
    size_t length;

    if (separator == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session directory is invalid");
        return false;
    }
    length = (size_t)(separator - chat->session_path);
    if (length >= directory_size) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Session directory is too long");
        return false;
    }
    memcpy(directory, chat->session_path, length);
    directory[length] = '\0';
    return true;
}

static time_t session_file_modified(const struct stat *status)
{
#if defined(__APPLE__)
    return status->st_mtimespec.tv_sec;
#elif defined(__linux__)
    return status->st_mtim.tv_sec;
#else
    return status->st_mtime;
#endif
}

static int compare_saved_sessions(const void *lhs, const void *rhs)
{
    const struct chat_saved_session *left = lhs;
    const struct chat_saved_session *right = rhs;

    if (left->modified != right->modified) {
        return left->modified > right->modified ? -1 : 1;
    }
    return strcmp(left->file_name, right->file_name);
}

static size_t collect_saved_sessions(const struct chat_session *chat,
                                     struct chat_saved_session sessions[],
                                     size_t capacity)
{
    char directory[CHAT_PATH_SIZE];
    const char *current_name;
    DIR *stream;
    struct dirent *entry;
    size_t count = 0;

    if (sessions == NULL || capacity == 0 ||
        !session_directory_path(chat, directory, sizeof(directory), NULL)) {
        return 0;
    }
    current_name = strrchr(chat->session_path, '/') + 1;
    stream = opendir(directory);
    if (stream == NULL) {
        return 0;
    }
    while ((entry = readdir(stream)) != NULL && count < capacity) {
        char path[CHAT_PATH_SIZE];
        struct stat status;
        struct telos_value *items;
        struct chat_saved_session *saved = &sessions[count];
        size_t name_size = strlen(entry->d_name);
        size_t identifier_size;

        if (name_size <= sizeof(".jsonl") - 1 ||
            name_size >= sizeof(saved->file_name) ||
            strcmp(entry->d_name + name_size - (sizeof(".jsonl") - 1),
                   ".jsonl") != 0 ||
            snprintf(path, sizeof(path), "%s/%s", directory,
                     entry->d_name) >= (int)sizeof(path) ||
            stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
            continue;
        }
        identifier_size = name_size - (sizeof(".jsonl") - 1);
        if (identifier_size == 0 || identifier_size >= sizeof(saved->identifier)) {
            continue;
        }
        memset(saved, 0, sizeof(*saved));
        memcpy(saved->file_name, entry->d_name, name_size + 1);
        memcpy(saved->identifier, entry->d_name, identifier_size);
        saved->identifier[identifier_size] = '\0';
        saved->modified = session_file_modified(&status);
        saved->current = strcmp(entry->d_name, current_name) == 0;
        items = read_session_file(path, saved->name, sizeof(saved->name), NULL);
        if (items != NULL) {
            if (saved->name[0] == '\0') {
                infer_session_name_from_items(items, saved->name,
                                              sizeof(saved->name));
            }
            infer_session_name_from_items(items, saved->preview,
                                          sizeof(saved->preview));
            telos_value_release(items);
        }
        if (saved->name[0] == '\0') {
            memcpy(saved->name, "New session", sizeof("New session"));
        }
        ++count;
    }
    closedir(stream);
    qsort(sessions, count, sizeof(sessions[0]), compare_saved_sessions);
    return count;
}

static bool session_completion_matches(const struct chat_saved_session *saved,
                                       const char *query)
{
    size_t query_size = query == NULL ? 0 : strlen(query);

    return query_size == 0 ||
           strncmp(saved->identifier, query, query_size) == 0 ||
           strncmp(saved->name, query, query_size) == 0;
}

static const char *resume_completion_query(const char *input)
{
    const char *query = input + sizeof("/resume") - 1U;

    while (*query == ' ' || *query == '\t') {
        ++query;
    }
    return query;
}

static size_t session_completion_count(const char *input, void *context)
{
    struct chat_session *chat = context;
    struct chat_saved_session sessions[CHAT_SESSION_COMPLETION_CAPACITY];
    const char *query;
    size_t count;
    size_t matches = 0;

    if (chat == NULL || input == NULL ||
        strncmp(input, "/resume", sizeof("/resume") - 1U) != 0) {
        return 0;
    }
    query = resume_completion_query(input);
    count = collect_saved_sessions(chat, sessions,
                                   sizeof(sessions) / sizeof(sessions[0]));
    for (size_t index = 0; index < count; ++index) {
        if (session_completion_matches(&sessions[index], query)) {
            ++matches;
        }
    }
    return matches;
}

static bool session_completion_at(
    const char *input, size_t ordinal,
    struct telos_frontend_completion_item *item, void *context)
{
    struct chat_session *chat = context;
    struct chat_saved_session sessions[CHAT_SESSION_COMPLETION_CAPACITY];
    const char *query;
    size_t count;

    if (chat == NULL || input == NULL || item == NULL ||
        strncmp(input, "/resume", sizeof("/resume") - 1U) != 0) {
        return false;
    }
    query = resume_completion_query(input);
    count = collect_saved_sessions(chat, sessions,
                                   sizeof(sessions) / sizeof(sessions[0]));
    for (size_t index = 0; index < count; ++index) {
        const struct chat_saved_session *saved = &sessions[index];
        int detail_size;

        if (!session_completion_matches(saved, query)) {
            continue;
        }
        if (ordinal > 0) {
            --ordinal;
            continue;
        }
        if (snprintf(item->value, sizeof(item->value), "/resume %s",
                     saved->identifier) >= (int)sizeof(item->value) ||
            snprintf(item->label, sizeof(item->label), "%s%s", saved->name,
                     saved->current ? " (current)" : "") >=
                (int)sizeof(item->label)) {
            return false;
        }
        detail_size = snprintf(item->detail, sizeof(item->detail), "%s%s%s",
                               saved->identifier,
                               saved->preview[0] == '\0' ? "" : " · ",
                               saved->preview);
        return detail_size >= 0 && detail_size < (int)sizeof(item->detail);
    }
    return false;
}

static bool sessions_command(const char *arguments,
                             const struct telos_cancel *cancel,
                             telos_frontend_emit_fn emit,
                             void *emit_context,
                             void *context,
                             struct telos_error **error)
{
    struct chat_session *chat = context;
    struct chat_saved_session sessions[CHAT_SESSION_COMPLETION_CAPACITY];
    char directory[CHAT_PATH_SIZE];
    char text[TELOS_COMMAND_ARGUMENT_SIZE] = "saved sessions:\n";
    size_t used = sizeof("saved sessions:\n") - 1;
    size_t count;

    (void)arguments;
    (void)cancel;
    if (!session_directory_path(chat, directory, sizeof(directory), error)) {
        return false;
    }
    count = collect_saved_sessions(
        chat, sessions, sizeof(sessions) / sizeof(sessions[0]));
    for (size_t index = 0; index < count; ++index) {
        int written;

        written = snprintf(text + used, sizeof(text) - used,
                           "  %s · %s%s\n", sessions[index].identifier,
                           sessions[index].name,
                           sessions[index].current ? " (current)" : "");
        if (written < 0 || (size_t)written >= sizeof(text) - used) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Session list is too large");
            return false;
        }
        used += (size_t)written;
    }
    if (count == 0) {
        memcpy(text, "no saved sessions", sizeof("no saved sessions"));
    }
    return emit_notice(emit, emit_context, text, error);
}

static bool session_file_newer(const struct stat *candidate,
                               const struct stat *current)
{
#if defined(__APPLE__)
    return candidate->st_mtimespec.tv_sec > current->st_mtimespec.tv_sec ||
           (candidate->st_mtimespec.tv_sec == current->st_mtimespec.tv_sec &&
            candidate->st_mtimespec.tv_nsec > current->st_mtimespec.tv_nsec);
#elif defined(__linux__)
    return candidate->st_mtim.tv_sec > current->st_mtim.tv_sec ||
           (candidate->st_mtim.tv_sec == current->st_mtim.tv_sec &&
            candidate->st_mtim.tv_nsec > current->st_mtim.tv_nsec);
#else
    return candidate->st_mtime > current->st_mtime;
#endif
}

static bool load_latest_session(struct chat_session *chat,
                                struct telos_error **error)
{
    char directory[CHAT_PATH_SIZE];
    const char *current_name;
    struct stat latest_status = {0};
    DIR *stream;
    struct dirent *entry;
    bool found = false;
    struct telos_value *latest_items = NULL;
    char latest_name[CHAT_SESSION_NAME_SIZE] = {0};

    if (!session_directory_path(chat, directory, sizeof(directory), error)) {
        return false;
    }
    current_name = strrchr(chat->session_path, '/') + 1;
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
            strcmp(entry->d_name, current_name) == 0 ||
            snprintf(path, sizeof(path), "%s/%s", directory,
                     entry->d_name) >= (int)sizeof(path) ||
            stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
            continue;
        }
        if (status.st_size == 0) {
            continue;
        }
        if (!found || session_file_newer(&status, &latest_status)) {
            char loaded_name[CHAT_SESSION_NAME_SIZE] = {0};
            struct telos_value *items = read_session_file(
                path, loaded_name, sizeof(loaded_name), error);

            if (items == NULL) {
                closedir(stream);
                telos_value_release(latest_items);
                return false;
            }
            if (telos_value_count(items) == 0) {
                telos_value_release(items);
                continue;
            }
            telos_value_release(latest_items);
            latest_items = items;
            memcpy(latest_name, loaded_name, sizeof(latest_name));
            latest_status = status;
            found = true;
        }
    }
    closedir(stream);
    if (!found || latest_items == NULL) {
        return true;
    }
    if (!replace_messages(chat, latest_items, error)) {
        telos_value_release(latest_items);
        return false;
    }
    if (!set_session_name(chat, latest_name, false, error) ||
        !ensure_session_name(chat, latest_items, NULL, true, error)) {
        telos_value_release(latest_items);
        return false;
    }
    telos_value_release(latest_items);
    return persist_session_snapshot(chat, error) &&
           checkpoint_messages(chat, error);
}

static bool resolve_resume_path(const struct chat_session *chat,
                                const char *arguments, char path[],
                                size_t path_size,
                                struct telos_error **error)
{
    char directory[CHAT_PATH_SIZE];
    struct telos_id identifier;

    if (arguments == NULL || arguments[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session name is required");
        return false;
    }
    if (strchr(arguments, '/') != NULL) {
        if (snprintf(path, path_size, "%s", arguments) >= (int)path_size) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Session path is too long");
            return false;
        }
        return true;
    }
    if (!session_directory_path(chat, directory, sizeof(directory), error)) {
        return false;
    }
    if (telos_id_parse(arguments, &identifier)) {
        if (snprintf(path, path_size, "%s/%s.jsonl", directory, arguments) >=
            (int)path_size) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "Session path is too long");
            return false;
        }
        return true;
    }
    if (snprintf(path, path_size, "%s/%s%s", directory, arguments,
                 strstr(arguments, ".jsonl") == NULL ? ".jsonl" : "") >=
        (int)path_size) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Session path is too long");
        return false;
    }
    return true;
}

static bool session_path_identifier(const struct chat_session *chat,
                                    const char *path,
                                    struct telos_id *identifier)
{
    char directory[CHAT_PATH_SIZE];
    const char *separator;
    const char *name;
    size_t directory_size;
    size_t name_size;

    if (!session_directory_path(chat, directory, sizeof(directory), NULL) ||
        path == NULL || identifier == NULL) {
        return false;
    }
    directory_size = strlen(directory);
    if (strlen(path) <= directory_size ||
        strncmp(path, directory, directory_size) != 0 ||
        path[directory_size] != '/') {
        return false;
    }
    separator = strrchr(path, '/');
    name = separator == NULL ? path : separator + 1;
    name_size = strlen(name);
    if (name_size != (TELOS_ID_TEXT_SIZE - 1) + sizeof(".jsonl") - 1 ||
        strcmp(name + TELOS_ID_TEXT_SIZE - 1, ".jsonl") != 0) {
        return false;
    }
    {
        char identifier_text[TELOS_ID_TEXT_SIZE];

        memcpy(identifier_text, name, TELOS_ID_TEXT_SIZE - 1);
        identifier_text[TELOS_ID_TEXT_SIZE - 1] = '\0';
        return telos_id_parse(identifier_text, identifier);
    }
}

static bool switch_persisted_session(struct chat_session *chat,
                                     const char *path,
                                     struct telos_id identifier,
                                     const struct telos_value *items,
                                     struct telos_error **error)
{
    struct telos_event_store *store = NULL;
    struct telos_event_store *previous;

    if (strcmp(path, chat->session_path) == 0) {
        if (!replace_messages(chat, items, error)) {
            return false;
        }
        return checkpoint_messages(chat, error);
    }
    store = telos_jsonl_store_create(path, error);
    if (store == NULL) {
        return false;
    }
    if (!replace_messages(chat, items, error)) {
        telos_event_store_destroy(store);
        return false;
    }
    previous = chat->session_store;
    chat->session_store = store;
    chat->session_id = identifier;
    chat->session_sequence = telos_event_store_count(store);
    memcpy(chat->session_path, path, strlen(path) + 1);
    telos_event_store_destroy(previous);
    return checkpoint_messages(chat, error);
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
    if (!set_session_name(chat, name, true, error)) {
        return false;
    }
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
    char path[CHAT_PATH_SIZE];
    struct telos_id identifier;
    char loaded_name[CHAT_SESSION_NAME_SIZE] = {0};
    bool persisted;
    bool result;

    (void)cancel;
    if (arguments != NULL && arguments[0] != '\0') {
        if (!resolve_resume_path(chat, arguments, path, sizeof(path), error)) {
            return false;
        }
        items = read_session_file(path, loaded_name, sizeof(loaded_name), error);
        if (items == NULL) {
            return false;
        }
        persisted = session_path_identifier(chat, path, &identifier);
        if (persisted) {
            result = switch_persisted_session(chat, path, identifier, items,
                                              error);
        } else {
            result = replace_messages(chat, items, error);
        }
        if (!result) {
            telos_value_release(items);
            return false;
        }
        if (!set_session_name(chat, loaded_name, false, error) ||
            !ensure_session_name(chat, items, NULL, persisted, error)) {
            telos_value_release(items);
            return false;
        }
        telos_value_release(items);
        if (!persisted &&
            (!persist_session_snapshot(chat, error) ||
             !checkpoint_messages(chat, error))) {
            return false;
        }
        return emit_notice(emit, emit_context, "session resumed", error);
    }
    if (chat->checkpoint_count == 0) {
        return emit_notice(
            emit, emit_context,
            "no session checkpoint exists; use /sessions then /resume SESSION",
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
    char loaded_name[CHAT_SESSION_NAME_SIZE] = {0};
    bool result;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Usage: /import PATH");
        return false;
    }
    items = read_session_file(arguments, loaded_name, sizeof(loaded_name),
                              error);
    if (items == NULL) {
        return false;
    }
    result = replace_messages(chat, items, error);
    if (result && !set_session_name(chat, loaded_name, false, error)) {
        result = false;
    }
    if (result && !ensure_session_name(chat, items, NULL, false, error)) {
        result = false;
    }
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
                 "provider=%s model=%s thinking=%s endpoint=%s status=%s",
                 chat_provider_get(chat), chat_model_get(chat),
                 chat->thinking_level, chat->configured_endpoint,
                 chat->status_spec) >=
        (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Settings summary is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool status_command(const char *arguments,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *context,
                           struct telos_error **error)
{
    struct chat_session *chat = context;
    char canonical[CHAT_STATUS_SPEC_SIZE];
    char message[CHAT_STATUS_SPEC_SIZE + 32];
    unsigned int fields;

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        if (snprintf(message, sizeof(message), "status=%s",
                     chat->status_spec) >= (int)sizeof(message)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Status summary is too long");
            return false;
        }
        return emit_notice(emit, emit_context, message, error);
    }
    if (!parse_status_spec(arguments, &fields, canonical, sizeof(canonical),
                           error) ||
        !telos_config_persist(chat->config, "agent.status", canonical,
                              error)) {
        return false;
    }
    chat->frontend_status.fields = fields;
    memcpy(chat->status_spec, canonical, strlen(canonical) + 1);
    if (snprintf(message, sizeof(message), "Status fields set to %s",
                 chat->status_spec) >= (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Status confirmation is too long");
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

static bool thinking_command_named(const char *arguments,
                                   const struct telos_cancel *cancel,
                                   telos_frontend_emit_fn emit,
                                   void *emit_context,
                                   void *context,
                                   const char *status_name,
                                   const char *display_name,
                                   struct telos_error **error)
{
    struct chat_session *chat = context;
    char level[CHAT_THINKING_LEVEL_SIZE];
    char message[128];

    (void)cancel;
    if (arguments == NULL || arguments[0] == '\0') {
        if (snprintf(message, sizeof(message), "%s=%s", status_name,
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
    if (!telos_config_persist(chat->config, "agent.thinking",
                              chat->thinking_level, error)) {
        return false;
    }
    if (snprintf(message, sizeof(message), "%s set to %s", display_name,
                 level) >= (int)sizeof(message)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Thinking status is too long");
        return false;
    }
    return emit_notice(emit, emit_context, message, error);
}

static bool thinking_command(const char *arguments,
                             const struct telos_cancel *cancel,
                             telos_frontend_emit_fn emit,
                             void *emit_context,
                             void *context,
                             struct telos_error **error)
{
    return thinking_command_named(arguments, cancel, emit, emit_context,
                                  context, "thinking", "Thinking level",
                                  error);
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
    char persisted_model[TELOS_COMMAND_ARGUMENT_SIZE];
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
            provider = model_provider_for(provider_name);
            model_id = separator + 1;
        }
        if (provider == NULL || provider[0] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "A Provider is required for a custom model");
            return false;
        }
        provider = model_provider_for(provider);
        if (provider == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOTSUP,
                      "The selected Provider Plugin is not available");
            return false;
        }
        model_id = migrate_model_id(provider, model_id);
        if (strlen(model_id) >= CHAT_MODEL_ID_SIZE) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Custom model identifier is too long");
            return false;
        }
        if (telos_model_catalog_find(&chat->model_catalog, provider, model_id) ==
            NULL) {
            if (chat->model_catalog.count >= TELOS_MODEL_CATALOG_CAPACITY) {
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
            if (!telos_model_catalog_add(&chat->model_catalog, &custom,
                                         error)) {
                return false;
            }
        }
        if (!telos_model_catalog_select(&chat->model_catalog, provider,
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
    if (snprintf(persisted_model, sizeof(persisted_model), "%s/%s",
                 model->provider, model->id) >= (int)sizeof(persisted_model) ||
        !telos_config_persist(chat->config, "agent.model", persisted_model,
                              error) ||
        !telos_config_persist(chat->config, "agent.thinking",
                              chat->thinking_level, error)) {
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

static bool setting_command(const char *arguments,
                            const struct telos_cancel *cancel,
                            telos_frontend_emit_fn emit,
                            void *emit_context,
                            void *context,
                            struct telos_error **error)
{
    char specification[TELOS_COMMAND_ARGUMENT_SIZE];
    char *value;
    char *separator;

    if (arguments == NULL || arguments[0] == '\0') {
        return settings_command(arguments, cancel, emit, emit_context,
                                context, error);
    }
    if (!copy_trimmed(arguments, specification, sizeof(specification), error)) {
        return false;
    }
    separator = strpbrk(specification, " \t");
    if (separator == NULL) {
        separator = specification + strlen(specification);
    } else {
        *separator = '\0';
        value = separator + 1;
        while (*value == ' ' || *value == '\t') {
            ++value;
        }
        if (strcmp(specification, "model") == 0) {
            return model_command(value, cancel, emit, emit_context, context,
                                 error);
        }
        if (strcmp(specification, "thinking") == 0) {
            return thinking_command(value, cancel, emit, emit_context,
                                    context, error);
        }
        if (strcmp(specification, "status") == 0) {
            return status_command(value, cancel, emit, emit_context, context,
                                  error);
        }
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Usage: /setting [model MODEL|thinking LEVEL|status FIELDS]");
        return false;
    }
    if (strcmp(specification, "model") == 0) {
        return model_command("", cancel, emit, emit_context, context, error);
    }
    if (strcmp(specification, "thinking") == 0) {
        return thinking_command("", cancel, emit, emit_context, context,
                                error);
    }
    if (strcmp(specification, "status") == 0) {
        return status_command("", cancel, emit, emit_context, context, error);
    }
    set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
              "Usage: /setting [model MODEL|thinking LEVEL|status FIELDS]");
    return false;
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

struct chat_steer_context {
    struct chat_session *chat;
    const struct telos_frontend_steer *frontend;
};

static struct telos_value *chat_agent_steer(
    void *context, const char *assistant_text, struct telos_error **error)
{
    struct chat_steer_context *steer_context = context;
    struct chat_session *chat;
    struct telos_value *message;
    char *text;

    if (steer_context == NULL || steer_context->chat == NULL ||
        steer_context->frontend == NULL || steer_context->frontend->next == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent steer context is invalid");
        return NULL;
    }
    chat = steer_context->chat;
    text = steer_context->frontend->next(steer_context->frontend->context,
                                         error);
    if (text == NULL) {
        return NULL;
    }
    message = create_message("user", text, error);
    if (message == NULL) {
        free(text);
        return NULL;
    }
    if ((assistant_text != NULL &&
         !append_message(chat, "assistant", assistant_text, error)) ||
        !append_message_value(chat, message, strlen(text), error)) {
        telos_value_release(message);
        free(text);
        return NULL;
    }
    free(text);
    return message;
}

static bool chat_turn(const char *input, const struct telos_cancel *cancel,
                      telos_frontend_emit_fn emit, void *emit_context,
                      const struct telos_frontend_steer *steer,
                      void *turn_context, struct telos_error **error)
{
    struct chat_session *chat = turn_context;
    struct chat_steer_context steer_context = {
        .chat = chat,
        .frontend = steer,
    };
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
    if (!ensure_session_name(chat, NULL, input, true, error)) {
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
            .steer = steer == NULL ? NULL : chat_agent_steer,
            .steer_context = &steer_context,
            .maximum_provider_rounds = 32,
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
            .name = "status",
            .help = "show or configure the footer status fields",
            .run = status_command,
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
            .name = "rename",
            .help = "rename the current session",
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
            .help = "resume a saved session, checkpoint, or exported file",
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
            .run = setting_command,
            .context = chat,
        },
        {
            .name = "setting",
            .help = "show or set common settings",
            .run = setting_command,
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
                "Ctrl+G opens $EDITOR · Ctrl+O toggles the tool panel",
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
    const char *provider = model_provider_for(provider_name);
    const char *model_id = model;
    char provider_storage[64];
    const char *separator;

    chat->configured_provider = provider;
    if (model == NULL || model[0] == '\0' || strcmp(model, "unconfigured") ==
                                               0) {
        return true;
    }
    separator = strchr(model, '/');
    if (separator != NULL) {
        size_t provider_size = (size_t)(separator - model);

        if (provider_size > 0 && provider_size < sizeof(provider_storage)) {
            memcpy(provider_storage, model, provider_size);
            provider_storage[provider_size] = '\0';
            {
                const char *candidate =
                    model_provider_for(provider_storage);

                if (model_provider_supported(candidate)) {
                    provider = candidate;
                    model_id = separator + 1;
                }
            }
        }
    }
    model_id = migrate_model_id(provider, model_id);
    if (provider == NULL || !model_provider_supported(provider) ||
        model_id[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Configured model selection is invalid");
        return false;
    }
    chat->configured_provider = provider;
    if (telos_model_catalog_find(&chat->model_catalog, provider, model_id) ==
        NULL) {
        const struct telos_model_descriptor custom = {
            .provider = provider,
            .id = model_id,
            .name = model_id,
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
    if (!telos_model_catalog_select(&chat->model_catalog, provider, model_id,
                                    error)) {
        return false;
    }
    chat->selected_model = telos_model_catalog_current(&chat->model_catalog);
    chat->model = chat->selected_model->variant_of == NULL
                      ? chat->selected_model->id
                      : chat->selected_model->variant_of;
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
    const char *status = telos_config_get(config, "agent.status");
    const char *authentication_endpoint =
        getenv("TELOS_OPENAI_AUTH_ENDPOINT");
    unsigned int status_fields;

    chat->home_directory = home_directory;
    chat->current_directory = current_directory;
    chat->config = config;
    if (thinking == NULL || !thinking_level_valid(thinking) ||
        strlen(thinking) >= sizeof(chat->thinking_level)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Agent thinking level is invalid");
        return false;
    }
    memcpy(chat->thinking_level, thinking, strlen(thinking) + 1);
    if (status == NULL ||
        !parse_status_spec(status, &status_fields, chat->status_spec,
                           sizeof(chat->status_spec), error)) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Agent status fields are invalid");
        }
        return false;
    }
    chat->frontend_status = (struct telos_frontend_status){
        .fields = status_fields,
        .thinking_get = chat_thinking_get,
        .branch_get = chat_branch_get,
        .context_used_get = chat_context_used_get,
        .context_window_get = chat_context_window_get,
        .home_directory = home_directory,
        .context = chat,
    };
    discover_git_branch(current_directory, chat->git_branch,
                        sizeof(chat->git_branch));
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
    if (chat->selected_model != NULL) {
        const char *level = chat->thinking_level;

        if ((chat->selected_model->capabilities &
             TELOS_MODEL_CAPABILITY_REASONING) == 0) {
            level = "off";
        }
        if (!set_thinking_options(chat, level, error)) {
            return false;
        }
        if (strcmp(level, chat->thinking_level) != 0) {
            memcpy(chat->thinking_level, level, strlen(level) + 1);
        }
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
                "/model  select model · /thinking  set reasoning level · "
                "/setting  show or set common settings",
            .initial_prompt = initial_prompt,
            .commands = &chat.commands,
            .completion_count = session_completion_count,
            .completion_at = session_completion_at,
            .completion_context = &chat,
            .model_catalog = &chat.model_catalog,
            .status = &chat.frontend_status,
            .provider_get = chat_provider_get,
            .model_get = chat_model_get,
            .identity_context = &chat,
            .single_turn = single_turn,
            .turn = chat_turn,
            .turn_context = &chat,
        };
        const struct telos_tui_frontend_config frontend = {
            .session = &session,
            .input_descriptor = STDIN_FILENO,
            .output_descriptor = STDOUT_FILENO,
            .force_plain = single_turn,
            .json_output = json_output,
            .rpc_mode = rpc_mode,
        };

        result = telos_tui_frontend_run(&frontend, error);
    }
    clear_chat(&chat);
    return result;
}
