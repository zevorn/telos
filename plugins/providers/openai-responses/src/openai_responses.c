#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/openai_responses.h>

#define TELOS_OPENAI_SSE_MAX_SIZE (1024U * 1024U)

struct tool_call {
    char *item_id;
    char *call_id;
};

struct telos_openai_sse_parser {
    enum telos_openai_unknown_event_policy unknown_policy;
    telos_provider_event_fn callback;
    void *callback_context;
    char *buffer;
    size_t size;
    size_t capacity;
    struct tool_call *tool_calls;
    size_t tool_call_count;
    size_t tool_call_capacity;
    bool finished;
};

struct telos_openai_responses_provider {
    char *model;
    char *endpoint;
    struct telos_secret_reference *secret_reference;
    struct telos_secret_broker *secret_broker;
    char **capabilities;
    size_t capability_count;
    telos_transport_send_fn send;
    void *transport_context;
    enum telos_openai_unknown_event_policy unknown_event_policy;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

struct telos_value *
telos_openai_responses_build_request(const char *model,
                                     const telos_provider_request *request,
                                     struct telos_error **error)
{
    const char **keys = NULL;
    struct telos_value **values = NULL;
    size_t option_count;
    size_t count = 0;
    struct telos_value *result = NULL;

    if (error != NULL) {
        *error = NULL;
    }
    if (model == NULL || model[0] == '\0' || request == NULL ||
        request->instructions == NULL || request->items == NULL ||
        telos_value_type(request->items) != TELOS_VALUE_ARRAY ||
        request->tools == NULL ||
        telos_value_type(request->tools) != TELOS_VALUE_ARRAY ||
        request->options == NULL ||
        telos_value_type(request->options) != TELOS_VALUE_OBJECT ||
        (request->state_mode != TELOS_PROVIDER_STATE_LOCAL &&
         request->state_mode != TELOS_PROVIDER_STATE_REMOTE)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Provider request is invalid");
        return NULL;
    }
    option_count = telos_value_count(request->options);
    if (option_count > SIZE_MAX - 7) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses request option count is too large");
        return NULL;
    }
    keys = calloc(option_count + 7, sizeof(*keys));
    values = calloc(option_count + 7, sizeof(*values));
    if (keys == NULL || values == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses request allocation failed");
        goto cleanup;
    }

    keys[count] = "model";
    values[count++] = telos_value_new_string(model);
    keys[count] = "instructions";
    values[count++] = telos_value_new_string(request->instructions);
    keys[count] = "input";
    values[count++] = telos_value_retain(request->items);
    keys[count] = "tools";
    values[count++] = telos_value_retain(request->tools);
    keys[count] = "store";
    values[count++] = telos_value_new_boolean(request->state_mode ==
                                              TELOS_PROVIDER_STATE_REMOTE);
    keys[count] = "stream";
    values[count++] = telos_value_new_boolean(true);
    if (request->state_mode == TELOS_PROVIDER_STATE_REMOTE &&
        request->previous_response_id != NULL &&
        request->previous_response_id[0] != '\0') {
        keys[count] = "previous_response_id";
        values[count++] = telos_value_new_string(request->previous_response_id);
    }
    for (size_t index = 0; index < option_count; ++index) {
        const char *key = telos_value_key_at(request->options, index);

        if (key == NULL || strcmp(key, "model") == 0 ||
            strcmp(key, "instructions") == 0 || strcmp(key, "input") == 0 ||
            strcmp(key, "tools") == 0 || strcmp(key, "store") == 0 ||
            strcmp(key, "stream") == 0 ||
            strcmp(key, "previous_response_id") == 0) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Responses option overrides a reserved field");
            goto cleanup;
        }
        keys[count] = key;
        values[count++] =
            telos_value_retain(telos_value_get(request->options, key));
    }
    for (size_t index = 0; index < count; ++index) {
        if (values[index] == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses request allocation failed");
            goto cleanup;
        }
    }
    result =
        telos_value_new_object(keys, (const struct telos_value *const *)values,
                               count);
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses request construction failed");
    }

cleanup:
    for (size_t index = 0; index < count; ++index) {
        telos_value_release(values[index]);
    }
    free(values);
    free(keys);
    return result;
}

static bool buffer_reserve(struct telos_openai_sse_parser *parser,
                           size_t additional)
{
    size_t required;
    size_t capacity;
    char *buffer;

    if (additional > SIZE_MAX - parser->size - 1) {
        return false;
    }
    required = parser->size + additional + 1;
    if (required <= parser->capacity) {
        return true;
    }
    capacity = parser->capacity == 0 ? 1024 : parser->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    buffer = realloc(parser->buffer, capacity);
    if (buffer == NULL) {
        return false;
    }
    parser->buffer = buffer;
    parser->capacity = capacity;
    return true;
}

static bool valid_utf8(const unsigned char *data, size_t size)
{
    for (size_t index = 0; index < size;) {
        uint32_t code_point;
        size_t length;

        if (data[index] <= 0x7f) {
            index += 1;
            continue;
        }
        if (data[index] >= 0xc2 && data[index] <= 0xdf) {
            code_point = data[index] & 0x1f;
            length = 2;
        } else if (data[index] >= 0xe0 && data[index] <= 0xef) {
            code_point = data[index] & 0x0f;
            length = 3;
        } else if (data[index] >= 0xf0 && data[index] <= 0xf4) {
            code_point = data[index] & 0x07;
            length = 4;
        } else {
            return false;
        }
        if (length > size - index) {
            return false;
        }
        for (size_t offset = 1; offset < length; ++offset) {
            if ((data[index + offset] & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (data[index + offset] & 0x3f);
        }
        if ((length == 2 && code_point < 0x80) ||
            (length == 3 && code_point < 0x800) ||
            (length == 4 && code_point < 0x10000) || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += length;
    }
    return true;
}

static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static const struct tool_call *
find_tool_call(const struct telos_openai_sse_parser *parser,
               const char *item_id)
{
    if (item_id == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < parser->tool_call_count; ++index) {
        if (strcmp(parser->tool_calls[index].item_id, item_id) == 0) {
            return &parser->tool_calls[index];
        }
    }
    return NULL;
}

static bool remember_tool_call(struct telos_openai_sse_parser *parser,
                               const char *item_id, const char *call_id,
                               struct telos_error **error)
{
    struct tool_call *calls;
    size_t capacity;
    struct tool_call *call;

    if (item_id == NULL || call_id == NULL ||
        find_tool_call(parser, item_id) != NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Responses Tool Call identifiers are invalid or duplicated");
        return false;
    }
    if (parser->tool_call_count == parser->tool_call_capacity) {
        capacity = parser->tool_call_capacity == 0
                       ? 4
                       : parser->tool_call_capacity * 2;
        if (capacity < parser->tool_call_capacity ||
            capacity > SIZE_MAX / sizeof(*calls)) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses Tool Call capacity overflow");
            return false;
        }
        calls = realloc(parser->tool_calls, capacity * sizeof(*calls));
        if (calls == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses Tool Call allocation failed");
            return false;
        }
        parser->tool_calls = calls;
        parser->tool_call_capacity = capacity;
    }
    call = &parser->tool_calls[parser->tool_call_count];
    call->item_id = copy_string(item_id);
    call->call_id = copy_string(call_id);
    if (call->item_id == NULL || call->call_id == NULL) {
        free(call->call_id);
        free(call->item_id);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses Tool Call ID allocation failed");
        return false;
    }
    parser->tool_call_count += 1;
    return true;
}

static bool emit(struct telos_openai_sse_parser *parser,
                 enum telos_provider_event_kind kind, const char *response_id,
                 const char *item_id, const char *call_id, const char *name,
                 const char *delta, const struct telos_value *payload,
                 struct telos_error **error)
{
    const struct telos_provider_event event = {
        .kind = kind,
        .response_id = response_id,
        .item_id = item_id,
        .call_id = call_id,
        .name = name,
        .delta = delta,
        .payload = payload,
    };

    if (!parser->callback(&event, parser->callback_context, error)) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ECANCELED,
                  "Provider Event callback rejected the Event");
        return false;
    }
    return true;
}

static const char *string_field(const struct telos_value *object,
                                const char *key)
{
    return telos_value_string(telos_value_get(object, key));
}

static bool map_event(struct telos_openai_sse_parser *parser,
                      const char *declared_type, const struct telos_value *data,
                      struct telos_error **error)
{
    const char *type = string_field(data, "type");

    if (type == NULL || (declared_type != NULL && declared_type[0] != '\0' &&
                         strcmp(declared_type, type) != 0)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "SSE event name does not match Responses JSON type");
        return false;
    }

    if (strcmp(type, "response.created") == 0) {
        const struct telos_value *response = telos_value_get(data, "response");
        const char *response_id = string_field(response, "id");

        return response_id != NULL &&
               emit(parser, TELOS_PROVIDER_RESPONSE_STARTED, response_id, NULL,
                    NULL, NULL, NULL, response, error);
    }
    if (strcmp(type, "response.output_item.added") == 0) {
        const struct telos_value *item = telos_value_get(data, "item");
        const char *item_id = string_field(item, "id");
        const char *item_type = string_field(item, "type");

        if (item_id == NULL || item_type == NULL ||
            !emit(parser, TELOS_PROVIDER_OUTPUT_ITEM_ADDED, NULL, item_id,
                  string_field(item, "call_id"), string_field(item, "name"),
                  NULL, item, error)) {
            return false;
        }
        if (strcmp(item_type, "function_call") == 0) {
            const char *call_id = string_field(item, "call_id");
            const char *name = string_field(item, "name");

            return name != NULL &&
                   remember_tool_call(parser, item_id, call_id, error) &&
                   emit(parser, TELOS_PROVIDER_TOOL_CALL_STARTED, NULL, item_id,
                        call_id, name, NULL, item, error);
        }
        return true;
    }
    if (strcmp(type, "response.function_call_arguments.delta") == 0) {
        const char *item_id = string_field(data, "item_id");
        const struct tool_call *call = find_tool_call(parser, item_id);
        const char *delta = string_field(data, "delta");

        if (call == NULL || delta == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Responses Tool argument delta has no matching Tool "
                      "Call");
            return false;
        }
        return emit(parser, TELOS_PROVIDER_TOOL_ARGUMENT_DELTA, NULL, item_id,
                    call->call_id, NULL, delta, data, error);
    }
    if (strcmp(type, "response.output_item.done") == 0) {
        const struct telos_value *item = telos_value_get(data, "item");
        const char *item_id = string_field(item, "id");
        const char *item_type = string_field(item, "type");
        const struct tool_call *call;

        if (item_type == NULL || strcmp(item_type, "function_call") != 0) {
            return true;
        }
        call = find_tool_call(parser, item_id);
        if (call == NULL || string_field(item, "call_id") == NULL ||
            strcmp(call->call_id, string_field(item, "call_id")) != 0) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "completed Responses Tool Call ID does not match");
            return false;
        }
        {
            const char *arguments = string_field(item, "arguments");
            struct telos_value *parsed_arguments;
            bool result;

            if (arguments == NULL) {
                set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "completed Responses Tool Call has no arguments");
                return false;
            }
            parsed_arguments =
                telos_value_parse_json(arguments, strlen(arguments), error);
            if (parsed_arguments == NULL ||
                telos_value_type(parsed_arguments) != TELOS_VALUE_OBJECT) {
                telos_value_release(parsed_arguments);
                set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "Responses Tool arguments are not a JSON object");
                return false;
            }
            result = emit(parser, TELOS_PROVIDER_TOOL_CALL_COMPLETED, NULL,
                          item_id, call->call_id, string_field(item, "name"),
                          arguments, parsed_arguments, error);
            telos_value_release(parsed_arguments);
            return result;
        }
    }
    if (strcmp(type, "response.output_text.delta") == 0) {
        const char *delta = string_field(data, "delta");

        return delta != NULL && emit(parser, TELOS_PROVIDER_TEXT_DELTA, NULL,
                                     string_field(data, "item_id"), NULL, NULL,
                                     delta, data, error);
    }
    if (strcmp(type, "response.reasoning_summary_text.delta") == 0 ||
        strcmp(type, "response.reasoning_text.delta") == 0) {
        return emit(parser, TELOS_PROVIDER_REASONING_ITEM, NULL,
                    string_field(data, "item_id"), NULL, NULL,
                    string_field(data, "delta"), data, error);
    }
    if (strcmp(type, "response.completed") == 0) {
        const struct telos_value *response = telos_value_get(data, "response");
        const struct telos_value *usage = telos_value_get(response, "usage");
        const char *response_id = string_field(response, "id");

        return response_id != NULL &&
               (usage == NULL ||
                emit(parser, TELOS_PROVIDER_USAGE_UPDATE, response_id, NULL,
                     NULL, NULL, NULL, usage, error)) &&
               emit(parser, TELOS_PROVIDER_RESPONSE_COMPLETED, response_id,
                    NULL, NULL, NULL, NULL, response, error);
    }
    if (strcmp(type, "response.failed") == 0 || strcmp(type, "error") == 0) {
        return emit(parser, TELOS_PROVIDER_ERROR, NULL, NULL, NULL, NULL, NULL,
                    data, error);
    }
    if (parser->unknown_policy == TELOS_OPENAI_UNKNOWN_EVENT_IGNORE) {
        return true;
    }
    set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTONOSUPPORT,
              "unknown Responses SSE Event type");
    return false;
}

static bool process_frame(struct telos_openai_sse_parser *parser,
                          const char *frame, size_t size,
                          struct telos_error **error)
{
    const char *cursor = frame;
    const char *end = frame + size;
    char *event_type = NULL;
    char *json = NULL;
    size_t json_size = 0;
    struct telos_value *data;
    bool result;

    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_size;

        if (line_end == NULL) {
            line_end = end;
        }
        line_size = (size_t)(line_end - cursor);
        if (line_size > 0 && cursor[line_size - 1] == '\r') {
            line_size -= 1;
        }
        if (line_size == 0 || cursor[0] == ':') {
            cursor = line_end < end ? line_end + 1 : end;
            continue;
        }
        if (line_size > 6 && memcmp(cursor, "event:", 6) == 0) {
            const char *value = cursor + 6;

            while (value < cursor + line_size && *value == ' ') {
                ++value;
            }
            free(event_type);
            event_type = malloc((size_t)(cursor + line_size - value) + 1);
            if (event_type == NULL) {
                goto memory_failure;
            }
            memcpy(event_type, value, (size_t)(cursor + line_size - value));
            event_type[cursor + line_size - value] = '\0';
        } else if (line_size >= 5 && memcmp(cursor, "data:", 5) == 0) {
            const char *value = cursor + 5;
            size_t value_size;
            char *new_json;

            if (value < cursor + line_size && *value == ' ') {
                ++value;
            }
            value_size = (size_t)(cursor + line_size - value);
            if (value_size > SIZE_MAX - json_size - 2) {
                goto memory_failure;
            }
            new_json = realloc(json, json_size + value_size + 2);
            if (new_json == NULL) {
                goto memory_failure;
            }
            json = new_json;
            if (json_size > 0) {
                json[json_size++] = '\n';
            }
            memcpy(json + json_size, value, value_size);
            json_size += value_size;
            json[json_size] = '\0';
        } else {
            free(json);
            free(event_type);
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "invalid SSE field");
            return false;
        }
        cursor = line_end < end ? line_end + 1 : end;
    }
    if (json == NULL) {
        free(event_type);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "SSE frame does not contain data");
        return false;
    }
    if (strcmp(json, "[DONE]") == 0) {
        free(json);
        free(event_type);
        return true;
    }
    if (!valid_utf8((const unsigned char *)json, json_size)) {
        free(json);
        free(event_type);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EILSEQ,
                  "SSE data is not valid UTF-8");
        return false;
    }

    data = telos_value_parse_json(json, json_size, error);
    free(json);
    if (data == NULL || telos_value_type(data) != TELOS_VALUE_OBJECT) {
        telos_value_release(data);
        free(event_type);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Responses SSE data is not a JSON object");
        return false;
    }
    result = map_event(parser, event_type, data, error);
    if (!result) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Responses SSE Event fields are invalid");
    }
    telos_value_release(data);
    free(event_type);
    return result;

memory_failure:
    free(json);
    free(event_type);
    set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
              "SSE frame allocation failed");
    return false;
}

static bool next_frame(const char *buffer, size_t size, size_t *frame_size,
                       size_t *consumed)
{
    for (size_t index = 0; index + 1 < size; ++index) {
        if (buffer[index] == '\n' && buffer[index + 1] == '\n') {
            *frame_size = index;
            *consumed = index + 2;
            return true;
        }
        if (index + 3 < size && buffer[index] == '\r' &&
            buffer[index + 1] == '\n' && buffer[index + 2] == '\r' &&
            buffer[index + 3] == '\n') {
            *frame_size = index;
            *consumed = index + 4;
            return true;
        }
    }
    return false;
}

telos_openai_sse_parser *
telos_openai_sse_parser_create(telos_openai_event_policy unknown_policy,
                               telos_provider_event_fn callback,
                               void *callback_context,
                               struct telos_error **error)
{
    struct telos_openai_sse_parser *parser;

    if (error != NULL) {
        *error = NULL;
    }
    if ((unknown_policy != TELOS_OPENAI_UNKNOWN_EVENT_IGNORE &&
         unknown_policy != TELOS_OPENAI_UNKNOWN_EVENT_ERROR) ||
        callback == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Responses SSE parser configuration is invalid");
        return NULL;
    }
    parser = calloc(1, sizeof(*parser));
    if (parser == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses SSE parser allocation failed");
        return NULL;
    }
    parser->unknown_policy = unknown_policy;
    parser->callback = callback;
    parser->callback_context = callback_context;
    return parser;
}

void telos_openai_sse_parser_destroy(struct telos_openai_sse_parser *parser)
{
    if (parser == NULL) {
        return;
    }
    for (size_t index = 0; index < parser->tool_call_count; ++index) {
        free(parser->tool_calls[index].call_id);
        free(parser->tool_calls[index].item_id);
    }
    free(parser->tool_calls);
    free(parser->buffer);
    free(parser);
}

bool telos_openai_sse_parser_feed(struct telos_openai_sse_parser *parser,
                                  const char *data, size_t size,
                                  struct telos_error **error)
{
    size_t frame_size;
    size_t consumed;

    if (error != NULL) {
        *error = NULL;
    }
    if (parser == NULL || (size > 0 && data == NULL) || parser->finished) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Responses SSE parser feed is invalid");
        return false;
    }
    if (size > TELOS_OPENAI_SSE_MAX_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EMSGSIZE,
                  "Responses SSE chunk exceeds the size limit");
        return false;
    }
    if (!buffer_reserve(parser, size)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses SSE buffer allocation failed");
        return false;
    }
    if (size > 0) {
        memcpy(parser->buffer + parser->size, data, size);
    }
    parser->size += size;
    parser->buffer[parser->size] = '\0';
    if (parser->size > TELOS_OPENAI_SSE_MAX_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EMSGSIZE,
                  "Responses SSE frame exceeds the size limit");
        return false;
    }

    while (next_frame(parser->buffer, parser->size, &frame_size, &consumed)) {
        if (frame_size > 0 &&
            !process_frame(parser, parser->buffer, frame_size, error)) {
            return false;
        }
        memmove(parser->buffer, parser->buffer + consumed,
                parser->size - consumed);
        parser->size -= consumed;
        parser->buffer[parser->size] = '\0';
    }
    return true;
}

bool telos_openai_sse_parser_finish(struct telos_openai_sse_parser *parser,
                                    struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (parser == NULL || parser->finished) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Responses SSE parser cannot finish");
        return false;
    }
    for (size_t index = 0; index < parser->size; ++index) {
        if (!isspace((unsigned char)parser->buffer[index])) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Responses SSE stream ended with a partial frame");
            return false;
        }
    }
    parser->finished = true;
    return true;
}

static void clear_capabilities(struct telos_openai_responses_provider *provider)
{
    for (size_t index = 0; index < provider->capability_count; ++index) {
        free(provider->capabilities[index]);
    }
    free(provider->capabilities);
}

static bool has_capability(const struct telos_openai_responses_config *config,
                           const char *required)
{
    for (size_t index = 0; index < config->capability_count; ++index) {
        if (config->capabilities[index] != NULL &&
            strcmp(config->capabilities[index], required) == 0) {
            return true;
        }
    }
    return false;
}

telos_openai_provider *
telos_openai_provider_create(const telos_openai_config *config,
                             struct telos_error **error)
{
    struct telos_openai_responses_provider *provider;

    if (error != NULL) {
        *error = NULL;
    }
    if (config != NULL &&
        config->capability_count > SIZE_MAX / sizeof(*provider->capabilities)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses Provider capability size overflow");
        return NULL;
    }
    if (config != NULL && config->capabilities != NULL) {
        for (size_t index = 0; index < config->capability_count; ++index) {
            if (config->capabilities[index] == NULL ||
                config->capabilities[index][0] == '\0') {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Responses Provider capability is invalid");
                return NULL;
            }
        }
    }
    if (config == NULL || config->model == NULL || config->model[0] == '\0' ||
        config->endpoint == NULL || config->endpoint[0] == '\0' ||
        config->secret_reference == NULL || config->secret_broker == NULL ||
        config->send == NULL ||
        (config->capability_count > 0 && config->capabilities == NULL) ||
        !has_capability(config, "network.https") ||
        (config->unknown_event_policy != TELOS_OPENAI_UNKNOWN_EVENT_IGNORE &&
         config->unknown_event_policy != TELOS_OPENAI_UNKNOWN_EVENT_ERROR)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Responses Provider configuration is invalid");
        return NULL;
    }
    provider = calloc(1, sizeof(*provider));
    if (provider == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses Provider allocation failed");
        return NULL;
    }
    provider->model = copy_string(config->model);
    provider->endpoint = copy_string(config->endpoint);
    provider->secret_reference =
        telos_secret_reference_create(config->secret_reference, error);
    if (provider->model == NULL || provider->endpoint == NULL ||
        provider->secret_reference == NULL) {
        telos_openai_provider_destroy(provider);
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses Provider metadata allocation failed");
        }
        return NULL;
    }
    if (config->capability_count > 0) {
        provider->capabilities =
            calloc(config->capability_count, sizeof(*provider->capabilities));
        if (provider->capabilities == NULL) {
            telos_openai_provider_destroy(provider);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses Provider capability allocation failed");
            return NULL;
        }
        for (size_t index = 0; index < config->capability_count; ++index) {
            provider->capabilities[index] =
                copy_string(config->capabilities[index]);
            if (provider->capabilities[index] == NULL) {
                provider->capability_count = index;
                telos_openai_provider_destroy(provider);
                set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                          "Responses Provider capability copy failed");
                return NULL;
            }
            provider->capability_count = index + 1;
        }
    }
    provider->secret_broker = config->secret_broker;
    provider->send = config->send;
    provider->transport_context = config->transport_context;
    provider->unknown_event_policy = config->unknown_event_policy;
    return provider;
}

void telos_openai_provider_destroy(telos_openai_provider *provider)
{
    if (provider == NULL) {
        return;
    }
    clear_capabilities(provider);
    telos_secret_reference_destroy(provider->secret_reference);
    free(provider->endpoint);
    free(provider->model);
    free(provider);
}

static bool receive_sse(const char *data, size_t size, void *context,
                        struct telos_error **error)
{
    return telos_openai_sse_parser_feed(context, data, size, error);
}

bool
telos_openai_provider_dispatch(const telos_provider_request *request,
                               telos_provider_event_fn emit_callback,
                               void *emit_context, void *provider_context,
                               struct telos_error **error)
{
    struct telos_openai_responses_provider *provider = provider_context;
    struct telos_value *body_value = NULL;
    size_t body_size;
    char *body = NULL;
    char *url = NULL;
    size_t url_size;
    struct telos_secret_material *secret = NULL;
    struct telos_openai_sse_parser *parser = NULL;
    int status_code = 0;
    bool result = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (provider == NULL || request == NULL || emit_callback == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Responses Provider dispatch arguments are invalid");
        return false;
    }
    body_value =
        telos_openai_responses_build_request(provider->model, request, error);
    body_size = telos_value_json_size(body_value);
    if (body_value == NULL || body_size == 0) {
        goto cleanup;
    }
    body = malloc(body_size);
    if (body == NULL ||
        !telos_value_write_json(body_value, body, body_size, NULL, error)) {
        if (body == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Responses request serialization allocation failed");
        }
        goto cleanup;
    }
    url_size = strlen(provider->endpoint) + strlen("/responses") + 1;
    url = malloc(url_size);
    if (url == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Responses endpoint allocation failed");
        goto cleanup;
    }
    snprintf(url, url_size, "%s%sresponses", provider->endpoint,
             provider->endpoint[strlen(provider->endpoint) - 1] == '/' ? ""
                                                                       : "/");
    secret =
        telos_secret_broker_resolve(provider->secret_broker,
                                    provider->secret_reference,
                                    "provider.openai",
                                    (const char *const *)provider->capabilities,
                                    provider->capability_count, true, error);
    if (secret == NULL) {
        goto cleanup;
    }
    parser = telos_openai_sse_parser_create(provider->unknown_event_policy,
                                            emit_callback, emit_context, error);
    if (parser == NULL) {
        goto cleanup;
    }
    {
        const struct telos_transport_request transport_request = {
            .method = "POST",
            .url = url,
            .content_type = "application/json",
            .bearer_token = telos_secret_material_data(secret),
            .body = body,
            .body_size = body_size - 1,
            .cancel = request->cancel,
        };

        if (!provider->send(&transport_request, receive_sse, parser,
                            &status_code, provider->transport_context, error)) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Responses transport failed");
            goto cleanup;
        }
    }
    if (status_code < 200 || status_code >= 300) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                  "Responses transport returned a non-success status");
        goto cleanup;
    }
    result = telos_openai_sse_parser_finish(parser, error);

cleanup:
    telos_openai_sse_parser_destroy(parser);
    telos_secret_material_destroy(secret);
    free(url);
    free(body);
    telos_value_release(body_value);
    return result;
}
