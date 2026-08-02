#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/openai_chat.h>

#define TELOS_OPENAI_CHAT_SSE_MAX_SIZE (1024U * 1024U)
#define TELOS_OPENAI_CHAT_MAXIMUM_HEADERS 16U
#define TELOS_OPENAI_CHAT_HEADER_SIZE 4096U
#define TELOS_OPENAI_CHAT_MAXIMUM_TOOL_CALLS 32U
#define TELOS_OPENAI_CHAT_MAXIMUM_ARGUMENTS (256U * 1024U)

struct chat_tool_call {
    size_t index;
    char *call_id;
    char *name;
    char *arguments;
    size_t argument_size;
    size_t argument_capacity;
    bool started;
    bool completed;
};

struct telos_openai_chat_sse_parser {
    enum telos_openai_chat_unknown_event_policy unknown_policy;
    telos_provider_event_fn callback;
    void *callback_context;
    char *buffer;
    size_t size;
    size_t capacity;
    char *response_id;
    struct chat_tool_call *tool_calls;
    size_t tool_call_count;
    bool response_started;
    bool finish_seen;
    bool response_completed;
    bool stream_done;
    bool failed;
    bool finished;
};

struct telos_openai_chat_provider {
    char *model;
    char *endpoint;
    char *secret_target;
    struct telos_secret_reference *secret_reference;
    struct telos_secret_broker *secret_broker;
    char **capabilities;
    size_t capability_count;
    struct telos_transport_header *headers;
    size_t header_count;
    telos_transport_send_fn send;
    void *transport_context;
    enum telos_openai_chat_unknown_event_policy unknown_event_policy;
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

static char *copy_string(const char *value)
{
    size_t size;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    size = strlen(value) + 1;
    copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static const char *string_field(const struct telos_value *object,
                                const char *key)
{
    return telos_value_string(telos_value_get(object, key));
}

static struct telos_value *new_message(const char *role,
                                       const char *content,
                                       struct telos_error **error)
{
    struct telos_value *role_value = telos_value_new_string(role);
    struct telos_value *content_value = telos_value_new_string(content);
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
                  "Chat message allocation failed");
    }
    return message;
}

static struct telos_value *convert_function_call(
    const struct telos_value *item,
    struct telos_error **error)
{
    const char *call_id = string_field(item, "call_id");
    const char *name = string_field(item, "name");
    const char *arguments = string_field(item, "arguments");
    struct telos_value *role = telos_value_new_string("assistant");
    struct telos_value *id =
        call_id == NULL ? NULL : telos_value_new_string(call_id);
    struct telos_value *type = telos_value_new_string("function");
    struct telos_value *function_name =
        name == NULL ? NULL : telos_value_new_string(name);
    struct telos_value *function_arguments =
        arguments == NULL ? NULL : telos_value_new_string(arguments);
    const char *function_keys[] = {"name", "arguments"};
    const struct telos_value *function_values[] = {
        function_name,
        function_arguments,
    };
    struct telos_value *function = NULL;
    const char *call_keys[] = {"id", "type", "function"};
    const struct telos_value *call_values[] = {id, type, function};
    struct telos_value *tool_call = NULL;
    const char *message_keys[] = {"role", "tool_calls"};
    const struct telos_value *message_values[] = {role, tool_call};
    struct telos_value *calls = NULL;
    struct telos_value *message = NULL;

    if (call_id == NULL || name == NULL || arguments == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat function call item is incomplete");
        goto cleanup;
    }
    function = telos_value_new_object(function_keys, function_values, 2);
    if (function == NULL) {
        goto cleanup;
    }
    call_values[2] = function;
    tool_call = telos_value_new_object(call_keys, call_values, 3);
    if (tool_call == NULL) {
        goto cleanup;
    }
    calls = telos_value_new_array(
        (const struct telos_value *const *)&tool_call, 1);
    if (calls == NULL) {
        goto cleanup;
    }
    message_values[1] = calls;
    message = telos_value_new_object(message_keys, message_values, 2);
    if (message == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat function call message allocation failed");
    }

cleanup:
    telos_value_release(calls);
    telos_value_release(tool_call);
    telos_value_release(function);
    telos_value_release(function_arguments);
    telos_value_release(function_name);
    telos_value_release(type);
    telos_value_release(id);
    telos_value_release(role);
    return message;
}

static struct telos_value *convert_function_output(
    const struct telos_value *item,
    struct telos_error **error)
{
    const char *call_id = string_field(item, "call_id");
    const char *output = string_field(item, "output");
    struct telos_value *role = telos_value_new_string("tool");
    struct telos_value *id =
        call_id == NULL ? NULL : telos_value_new_string(call_id);
    struct telos_value *content =
        output == NULL ? NULL : telos_value_new_string(output);
    const char *keys[] = {"role", "tool_call_id", "content"};
    const struct telos_value *values[] = {role, id, content};
    struct telos_value *message = NULL;

    if (call_id == NULL || output == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat function output item is incomplete");
    } else {
        message = telos_value_new_object(keys, values, 3);
        if (message == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat function output message allocation failed");
        }
    }
    telos_value_release(content);
    telos_value_release(id);
    telos_value_release(role);
    return message;
}

static struct telos_value *convert_item(const struct telos_value *item,
                                        struct telos_error **error)
{
    const char *type;

    if (item == NULL || telos_value_type(item) != TELOS_VALUE_OBJECT) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat message item must be an object");
        return NULL;
    }
    if (string_field(item, "role") != NULL &&
        string_field(item, "content") != NULL) {
        return telos_value_retain(item);
    }
    type = string_field(item, "type");
    if (type != NULL && strcmp(type, "function_call") == 0) {
        return convert_function_call(item, error);
    }
    if (type != NULL && strcmp(type, "function_call_output") == 0) {
        return convert_function_output(item, error);
    }
    set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
              "Chat message item has an unsupported shape");
    return NULL;
}

struct telos_value *telos_openai_chat_build_request(
    const char *model,
    const struct telos_provider_request *request,
    struct telos_error **error)
{
    const size_t fixed_fields = 4;
    const char **keys = NULL;
    struct telos_value **values = NULL;
    struct telos_value **messages = NULL;
    struct telos_value *system = NULL;
    struct telos_value *message_array = NULL;
    struct telos_value *result = NULL;
    size_t item_count;
    size_t option_count;
    size_t message_count = 0;
    size_t count = 0;

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
                  "Chat Provider request is invalid");
        return NULL;
    }
    item_count = telos_value_count(request->items);
    option_count = telos_value_count(request->options);
    if (item_count == SIZE_MAX || item_count + 1 < item_count ||
        option_count > SIZE_MAX - fixed_fields ||
        item_count + 1 > SIZE_MAX / sizeof(*messages)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat request size is too large");
        return NULL;
    }
    keys = calloc(option_count + fixed_fields, sizeof(*keys));
    values = calloc(option_count + fixed_fields, sizeof(*values));
    messages = calloc(item_count + 1, sizeof(*messages));
    if (keys == NULL || values == NULL || messages == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat request allocation failed");
        goto cleanup;
    }
    system = new_message("system", request->instructions, error);
    if (system == NULL) {
        goto cleanup;
    }
    messages[message_count++] = system;
    for (size_t index = 0; index < item_count; ++index) {
        messages[message_count] =
            convert_item(telos_value_at(request->items, index), error);
        if (messages[message_count] == NULL) {
            goto cleanup;
        }
        message_count += 1;
    }
    message_array = telos_value_new_array(
        (const struct telos_value *const *)messages, message_count);
    if (message_array == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat messages construction failed");
        goto cleanup;
    }

    keys[count] = "model";
    values[count++] = telos_value_new_string(model);
    keys[count] = "messages";
    values[count++] = telos_value_retain(message_array);
    keys[count] = "tools";
    values[count++] = telos_value_retain(request->tools);
    keys[count] = "stream";
    values[count++] = telos_value_new_boolean(true);
    for (size_t index = 0; index < option_count; ++index) {
        const char *key = telos_value_key_at(request->options, index);

        if (key == NULL || strcmp(key, "model") == 0 ||
            strcmp(key, "messages") == 0 || strcmp(key, "tools") == 0 ||
            strcmp(key, "stream") == 0) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Chat option overrides a reserved field");
            goto cleanup;
        }
        keys[count] = key;
        values[count++] =
            telos_value_retain(telos_value_get(request->options, key));
    }
    for (size_t index = 0; index < count; ++index) {
        if (values[index] == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat request value allocation failed");
            goto cleanup;
        }
    }
    result = telos_value_new_object(
        keys, (const struct telos_value *const *)values, count);
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat request construction failed");
    }

cleanup:
    for (size_t index = 0; index < count; ++index) {
        telos_value_release(values[index]);
    }
    for (size_t index = 0; index < message_count; ++index) {
        telos_value_release(messages[index]);
    }
    telos_value_release(message_array);
    free(messages);
    free(values);
    free(keys);
    return result;
}

static bool buffer_reserve(struct telos_openai_chat_sse_parser *parser,
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

static bool emit_event(struct telos_openai_chat_sse_parser *parser,
                       enum telos_provider_event_kind kind,
                       const char *response_id,
                       const char *call_id,
                       const char *name,
                       const char *delta,
                       const struct telos_value *payload,
                       struct telos_error **error)
{
    const struct telos_provider_event event = {
        .kind = kind,
        .response_id = response_id,
        .call_id = call_id,
        .name = name,
        .delta = delta,
        .payload = payload,
    };

    if (!parser->callback(&event, parser->callback_context, error)) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ECANCELED,
                  "Chat Provider Event callback rejected the Event");
        return false;
    }
    return true;
}

static struct chat_tool_call *find_tool_call(
    struct telos_openai_chat_sse_parser *parser,
    size_t index)
{
    for (size_t offset = 0; offset < parser->tool_call_count; ++offset) {
        if (parser->tool_calls[offset].index == index) {
            return &parser->tool_calls[offset];
        }
    }
    return NULL;
}

static struct chat_tool_call *get_tool_call(
    struct telos_openai_chat_sse_parser *parser,
    size_t index,
    struct telos_error **error)
{
    struct chat_tool_call *call = find_tool_call(parser, index);

    if (call != NULL) {
        return call;
    }
    if (parser->tool_call_count >= TELOS_OPENAI_CHAT_MAXIMUM_TOOL_CALLS) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, E2BIG,
                  "Chat Tool Call count exceeds the limit");
        return NULL;
    }
    call = &parser->tool_calls[parser->tool_call_count++];
    memset(call, 0, sizeof(*call));
    call->index = index;
    return call;
}

static bool set_owned_string(char **destination, const char *value,
                             struct telos_error **error)
{
    char *copy;

    if (value == NULL || value[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat Tool Call identifier is empty");
        return false;
    }
    if (*destination != NULL) {
        if (strcmp(*destination, value) != 0) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call identifier changed in the stream");
            return false;
        }
        return true;
    }
    copy = copy_string(value);
    if (copy == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat Tool Call identifier allocation failed");
        return false;
    }
    *destination = copy;
    return true;
}

static bool append_arguments(struct chat_tool_call *call, const char *delta,
                             struct telos_error **error)
{
    size_t delta_size = strlen(delta);
    size_t required;
    size_t capacity;
    char *arguments;

    if (delta_size > TELOS_OPENAI_CHAT_MAXIMUM_ARGUMENTS -
                         call->argument_size - 1) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EFBIG,
                  "Chat Tool Call arguments exceed the limit");
        return false;
    }
    required = call->argument_size + delta_size + 1;
    if (required > call->argument_capacity) {
        capacity = call->argument_capacity == 0 ? 128 : call->argument_capacity;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        arguments = realloc(call->arguments, capacity);
        if (arguments == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat Tool Call arguments allocation failed");
            return false;
        }
        call->arguments = arguments;
        call->argument_capacity = capacity;
    }
    memcpy(call->arguments + call->argument_size, delta, delta_size);
    call->argument_size += delta_size;
    call->arguments[call->argument_size] = '\0';
    return true;
}

static bool start_tool_call(struct telos_openai_chat_sse_parser *parser,
                            struct chat_tool_call *call,
                            const struct telos_value *payload,
                            struct telos_error **error)
{
    if (call->started) {
        return true;
    }
    if (call->call_id == NULL || call->name == NULL) {
        return true;
    }
    call->started = true;
    return emit_event(parser, TELOS_PROVIDER_TOOL_CALL_STARTED, NULL,
                      call->call_id, call->name, NULL, payload, error);
}

static bool map_tool_calls(struct telos_openai_chat_sse_parser *parser,
                           const struct telos_value *delta,
                           struct telos_error **error)
{
    const struct telos_value *calls = telos_value_get(delta, "tool_calls");

    if (calls == NULL) {
        return true;
    }
    if (telos_value_type(calls) != TELOS_VALUE_ARRAY) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat tool_calls delta is not an array");
        return false;
    }
    for (size_t offset = 0; offset < telos_value_count(calls); ++offset) {
        const struct telos_value *entry = telos_value_at(calls, offset);
        const struct telos_value *function;
        const struct telos_value *index_value;
        int64_t signed_index;
        const char *id;
        const char *name;
        const char *arguments;
        struct chat_tool_call *call;

        if (entry == NULL || telos_value_type(entry) != TELOS_VALUE_OBJECT) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call delta is not an object");
            return false;
        }
        index_value = telos_value_get(entry, "index");
        if (!telos_value_integer(index_value, &signed_index) ||
            signed_index < 0 || (uint64_t)signed_index > SIZE_MAX) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call index is invalid");
            return false;
        }
        call = get_tool_call(parser, (size_t)signed_index, error);
        if (call == NULL) {
            return false;
        }
        id = string_field(entry, "id");
        if (id != NULL && !set_owned_string(&call->call_id, id, error)) {
            return false;
        }
        function = telos_value_get(entry, "function");
        if (function != NULL &&
            telos_value_type(function) != TELOS_VALUE_OBJECT) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call function is not an object");
            return false;
        }
        name = string_field(function, "name");
        if (name != NULL && !set_owned_string(&call->name, name, error)) {
            return false;
        }
        if (!start_tool_call(parser, call, entry, error)) {
            return false;
        }
        arguments = string_field(function, "arguments");
        if (arguments != NULL) {
            size_t before = call->argument_size;

            if (!append_arguments(call, arguments, error)) {
                return false;
            }
            if (call->argument_size > before &&
                !emit_event(parser, TELOS_PROVIDER_TOOL_ARGUMENT_DELTA, NULL,
                            call->call_id, call->name, arguments, entry,
                            error)) {
                return false;
            }
        }
    }
    return true;
}

static bool finish_tool_calls(struct telos_openai_chat_sse_parser *parser,
                              struct telos_error **error)
{
    for (size_t index = 0; index < parser->tool_call_count; ++index) {
        struct chat_tool_call *call = &parser->tool_calls[index];
        const char *arguments =
            call->argument_size == 0 ? "{}" : call->arguments;
        struct telos_value *parsed;

        if (call->completed) {
            continue;
        }
        if (!call->started || call->call_id == NULL || call->name == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call ended before its identifiers arrived");
            return false;
        }
        parsed = telos_value_parse_json(arguments, strlen(arguments), error);
        if (parsed == NULL || telos_value_type(parsed) != TELOS_VALUE_OBJECT) {
            telos_value_release(parsed);
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat Tool Call arguments are not a JSON object");
            return false;
        }
        if (!emit_event(parser, TELOS_PROVIDER_TOOL_CALL_COMPLETED, NULL,
                        call->call_id, call->name, arguments, parsed, error)) {
            telos_value_release(parsed);
            return false;
        }
        telos_value_release(parsed);
        call->completed = true;
    }
    return true;
}

static bool ensure_response(struct telos_openai_chat_sse_parser *parser,
                            const char *response_id,
                            struct telos_error **error)
{
    if (response_id == NULL || response_id[0] == '\0') {
        if (!parser->response_started) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat response has no identifier");
            return false;
        }
        return true;
    }
    if (parser->response_id != NULL &&
        strcmp(parser->response_id, response_id) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat response identifier changed in the stream");
        return false;
    }
    if (!parser->response_started) {
        parser->response_id = copy_string(response_id);
        if (parser->response_id == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat response identifier allocation failed");
            return false;
        }
        parser->response_started = true;
        if (!emit_event(parser, TELOS_PROVIDER_RESPONSE_STARTED,
                        parser->response_id, NULL, NULL, NULL, NULL, error)) {
            return false;
        }
    }
    return true;
}

static bool map_data(struct telos_openai_chat_sse_parser *parser,
                     const struct telos_value *data,
                     struct telos_error **error)
{
    const char *id = string_field(data, "id");
    const struct telos_value *error_value = telos_value_get(data, "error");
    const struct telos_value *choices = telos_value_get(data, "choices");
    const struct telos_value *usage = telos_value_get(data, "usage");

    if (error_value != NULL) {
        if (!ensure_response(parser, id, error) ||
            !emit_event(parser, TELOS_PROVIDER_ERROR, parser->response_id,
                        NULL, NULL, NULL, error_value, error)) {
            return false;
        }
        parser->failed = true;
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat Provider returned an error");
        return false;
    }
    if (!ensure_response(parser, id, error)) {
        return false;
    }
    if (choices != NULL) {
        if (telos_value_type(choices) != TELOS_VALUE_ARRAY) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat choices is not an array");
            return false;
        }
        if (telos_value_count(choices) > 0) {
            const struct telos_value *choice = telos_value_at(choices, 0);
            const struct telos_value *delta;
            const char *finish_reason;
            const char *content;
            const char *reasoning;

            if (choice == NULL ||
                telos_value_type(choice) != TELOS_VALUE_OBJECT) {
                set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "Chat choice is not an object");
                return false;
            }
            delta = telos_value_get(choice, "delta");
            if (delta == NULL ||
                telos_value_type(delta) != TELOS_VALUE_OBJECT) {
                set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "Chat choice delta is not an object");
                return false;
            }
            content = string_field(delta, "content");
            if (content != NULL &&
                !emit_event(parser, TELOS_PROVIDER_TEXT_DELTA, NULL, NULL,
                            NULL, content, delta, error)) {
                return false;
            }
            reasoning = string_field(delta, "reasoning_content");
            if (reasoning != NULL &&
                !emit_event(parser, TELOS_PROVIDER_REASONING_ITEM, NULL, NULL,
                            NULL, reasoning, delta, error)) {
                return false;
            }
            if (!map_tool_calls(parser, delta, error)) {
                return false;
            }
            finish_reason = string_field(choice, "finish_reason");
            if (finish_reason != NULL) {
                parser->finish_seen = true;
                if (!finish_tool_calls(parser, error)) {
                    return false;
                }
            }
        }
    }
    if (usage != NULL) {
        if (telos_value_type(usage) != TELOS_VALUE_OBJECT ||
            !emit_event(parser, TELOS_PROVIDER_USAGE_UPDATE,
                        parser->response_id, NULL, NULL, NULL, usage,
                        error)) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat usage is invalid");
            return false;
        }
    }
    return true;
}

static bool process_frame(struct telos_openai_chat_sse_parser *parser,
                          const char *frame,
                          size_t size,
                          struct telos_error **error)
{
    const char *cursor = frame;
    const char *end = frame + size;
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
        if (line_size >= 5 && memcmp(cursor, "data:", 5) == 0) {
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
        } else if (line_size >= 6 && memcmp(cursor, "event:", 6) == 0) {
            /* Chat Completions does not use event names. */
        } else {
            free(json);
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "invalid Chat SSE field");
            return false;
        }
        cursor = line_end < end ? line_end + 1 : end;
    }
    if (json == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat SSE frame does not contain data");
        return false;
    }
    if (strcmp(json, "[DONE]") == 0) {
        parser->stream_done = true;
        free(json);
        return true;
    }
    if (!valid_utf8((const unsigned char *)json, json_size)) {
        free(json);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EILSEQ,
                  "Chat SSE data is not valid UTF-8");
        return false;
    }
    data = telos_value_parse_json(json, json_size, error);
    free(json);
    if (data == NULL || telos_value_type(data) != TELOS_VALUE_OBJECT) {
        telos_value_release(data);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "Chat SSE data is not a JSON object");
        return false;
    }
    result = map_data(parser, data, error);
    telos_value_release(data);
    return result;

memory_failure:
    free(json);
    set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
              "Chat SSE frame allocation failed");
    return false;
}

static bool next_frame(const char *buffer,
                       size_t size,
                       size_t *frame_size,
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

telos_openai_chat_sse_parser *telos_openai_chat_sse_parser_create(
    enum telos_openai_chat_unknown_event_policy unknown_policy,
    telos_provider_event_fn callback,
    void *callback_context,
    struct telos_error **error)
{
    struct telos_openai_chat_sse_parser *parser;

    if (error != NULL) {
        *error = NULL;
    }
    if ((unknown_policy != TELOS_OPENAI_CHAT_UNKNOWN_EVENT_IGNORE &&
         unknown_policy != TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR) ||
        callback == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat SSE parser configuration is invalid");
        return NULL;
    }
    parser = calloc(1, sizeof(*parser));
    if (parser == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat SSE parser allocation failed");
        return NULL;
    }
    parser->tool_calls = calloc(TELOS_OPENAI_CHAT_MAXIMUM_TOOL_CALLS,
                                sizeof(*parser->tool_calls));
    if (parser->tool_calls == NULL) {
        free(parser);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat SSE Tool Call allocation failed");
        return NULL;
    }
    parser->unknown_policy = unknown_policy;
    parser->callback = callback;
    parser->callback_context = callback_context;
    return parser;
}

void telos_openai_chat_sse_parser_destroy(telos_openai_chat_sse_parser *parser)
{
    if (parser == NULL) {
        return;
    }
    for (size_t index = 0; index < parser->tool_call_count; ++index) {
        free(parser->tool_calls[index].arguments);
        free(parser->tool_calls[index].name);
        free(parser->tool_calls[index].call_id);
    }
    free(parser->tool_calls);
    free(parser->response_id);
    free(parser->buffer);
    free(parser);
}

bool telos_openai_chat_sse_parser_feed(telos_openai_chat_sse_parser *parser,
                                       const char *data,
                                       size_t size,
                                       struct telos_error **error)
{
    size_t frame_size;
    size_t consumed;

    if (error != NULL) {
        *error = NULL;
    }
    if (parser == NULL || (size > 0 && data == NULL) || parser->finished ||
        parser->stream_done) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat SSE parser feed is invalid");
        return false;
    }
    if (size > TELOS_OPENAI_CHAT_SSE_MAX_SIZE ||
        size > TELOS_OPENAI_CHAT_SSE_MAX_SIZE - parser->size) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EMSGSIZE,
                  "Chat SSE chunk exceeds the size limit");
        return false;
    }
    if (!buffer_reserve(parser, size)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat SSE buffer allocation failed");
        return false;
    }
    if (size > 0) {
        memcpy(parser->buffer + parser->size, data, size);
    }
    parser->size += size;
    parser->buffer[parser->size] = '\0';
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

bool telos_openai_chat_sse_parser_finish(telos_openai_chat_sse_parser *parser,
                                         struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (parser == NULL || parser->finished) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat SSE parser cannot finish");
        return false;
    }
    for (size_t index = 0; index < parser->size; ++index) {
        if (!isspace((unsigned char)parser->buffer[index])) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat SSE stream ended with a partial frame");
            return false;
        }
    }
    if (parser->failed || !parser->response_started ||
        !finish_tool_calls(parser, error)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "Chat SSE stream did not produce a response");
        }
        return false;
    }
    if (!parser->response_completed &&
        !emit_event(parser, TELOS_PROVIDER_RESPONSE_COMPLETED,
                    parser->response_id, NULL, NULL, NULL, NULL, error)) {
        return false;
    }
    parser->response_completed = true;
    parser->finished = true;
    return true;
}

static bool has_capability(const struct telos_openai_chat_config *config,
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

static void clear_capabilities(telos_openai_chat_provider *provider)
{
    for (size_t index = 0; index < provider->capability_count; ++index) {
        free(provider->capabilities[index]);
    }
    free(provider->capabilities);
}

static void clear_headers(telos_openai_chat_provider *provider)
{
    for (size_t index = 0; index < provider->header_count; ++index) {
        free((char *)provider->headers[index].value);
        free((char *)provider->headers[index].name);
    }
    free(provider->headers);
}

telos_openai_chat_provider *telos_openai_chat_provider_create(
    const struct telos_openai_chat_config *config,
    struct telos_error **error)
{
    telos_openai_chat_provider *provider;

    if (error != NULL) {
        *error = NULL;
    }
    if (config != NULL &&
        config->capability_count > SIZE_MAX / sizeof(*provider->capabilities)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat Provider capability size overflow");
        return NULL;
    }
    if (config != NULL &&
        config->header_count > TELOS_OPENAI_CHAT_MAXIMUM_HEADERS) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat Provider header count is invalid");
        return NULL;
    }
    if (config != NULL && config->capabilities != NULL) {
        for (size_t index = 0; index < config->capability_count; ++index) {
            if (config->capabilities[index] == NULL ||
                config->capabilities[index][0] == '\0') {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Chat Provider capability is invalid");
                return NULL;
            }
        }
    }
    if (config != NULL && config->headers != NULL) {
        for (size_t index = 0; index < config->header_count; ++index) {
            size_t name_size;
            size_t value_size;

            if (config->headers[index].name == NULL ||
                config->headers[index].name[0] == '\0' ||
                config->headers[index].value == NULL ||
                strpbrk(config->headers[index].name, "\r\n") != NULL ||
                strpbrk(config->headers[index].value, "\r\n") != NULL) {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Chat Provider header is invalid");
                return NULL;
            }
            name_size = strlen(config->headers[index].name);
            value_size = strlen(config->headers[index].value);
            if (name_size > TELOS_OPENAI_CHAT_HEADER_SIZE - 3 ||
                value_size > TELOS_OPENAI_CHAT_HEADER_SIZE - name_size - 3) {
                set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                          "Chat Provider header is too long");
                return NULL;
            }
        }
    }
    if (config == NULL || config->model == NULL || config->model[0] == '\0' ||
        config->endpoint == NULL || config->endpoint[0] == '\0' ||
        config->secret_reference == NULL || config->secret_target == NULL ||
        config->secret_target[0] == '\0' || config->secret_broker == NULL ||
        config->send == NULL ||
        (config->capability_count > 0 && config->capabilities == NULL) ||
        (config->header_count > 0 && config->headers == NULL) ||
        !has_capability(config, "network.https") ||
        (config->unknown_event_policy !=
             TELOS_OPENAI_CHAT_UNKNOWN_EVENT_IGNORE &&
         config->unknown_event_policy !=
             TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat Provider configuration is invalid");
        return NULL;
    }
    provider = calloc(1, sizeof(*provider));
    if (provider == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat Provider allocation failed");
        return NULL;
    }
    provider->model = copy_string(config->model);
    provider->endpoint = copy_string(config->endpoint);
    provider->secret_target = copy_string(config->secret_target);
    provider->secret_reference =
        telos_secret_reference_create(config->secret_reference, error);
    if (provider->model == NULL || provider->endpoint == NULL ||
        provider->secret_target == NULL || provider->secret_reference == NULL) {
        telos_openai_chat_provider_destroy(provider);
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat Provider metadata allocation failed");
        }
        return NULL;
    }
    if (config->capability_count > 0) {
        provider->capabilities =
            calloc(config->capability_count, sizeof(*provider->capabilities));
        if (provider->capabilities == NULL) {
            telos_openai_chat_provider_destroy(provider);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat Provider capability allocation failed");
            return NULL;
        }
        for (size_t index = 0; index < config->capability_count; ++index) {
            provider->capabilities[index] =
                copy_string(config->capabilities[index]);
            if (provider->capabilities[index] == NULL) {
                provider->capability_count = index;
                telos_openai_chat_provider_destroy(provider);
                set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                          "Chat Provider capability copy failed");
                return NULL;
            }
            provider->capability_count = index + 1;
        }
    }
    if (config->header_count > 0) {
        provider->headers =
            calloc(config->header_count, sizeof(*provider->headers));
        if (provider->headers == NULL) {
            telos_openai_chat_provider_destroy(provider);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat Provider header allocation failed");
            return NULL;
        }
        for (size_t index = 0; index < config->header_count; ++index) {
            provider->headers[index].name =
                copy_string(config->headers[index].name);
            provider->headers[index].value =
                copy_string(config->headers[index].value);
            provider->header_count = index + 1;
            if (provider->headers[index].name == NULL ||
                provider->headers[index].value == NULL) {
                telos_openai_chat_provider_destroy(provider);
                set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                          "Chat Provider header copy failed");
                return NULL;
            }
        }
    }
    provider->secret_broker = config->secret_broker;
    provider->send = config->send;
    provider->transport_context = config->transport_context;
    provider->unknown_event_policy = config->unknown_event_policy;
    return provider;
}

void telos_openai_chat_provider_destroy(telos_openai_chat_provider *provider)
{
    if (provider == NULL) {
        return;
    }
    clear_headers(provider);
    clear_capabilities(provider);
    telos_secret_reference_destroy(provider->secret_reference);
    free(provider->secret_target);
    free(provider->endpoint);
    free(provider->model);
    free(provider);
}

static bool receive_sse(const char *data,
                        size_t size,
                        void *context,
                        struct telos_error **error)
{
    return telos_openai_chat_sse_parser_feed(context, data, size, error);
}

static char *endpoint_url(const char *endpoint, struct telos_error **error)
{
    static const char suffix[] = "chat/completions";
    size_t endpoint_size = strlen(endpoint);
    size_t suffix_size = sizeof(suffix) - 1;
    size_t separator = endpoint_size > 0 && endpoint[endpoint_size - 1] == '/'
                           ? 0
                           : 1;
    char *url;

    if (endpoint_size > suffix_size &&
        endpoint[endpoint_size - suffix_size - 1] == '/' &&
        strcmp(endpoint + endpoint_size - suffix_size, suffix) == 0) {
        return copy_string(endpoint);
    }
    if (endpoint_size > SIZE_MAX - suffix_size - separator - 1) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat endpoint size overflow");
        return NULL;
    }
    url = malloc(endpoint_size + separator + suffix_size + 1);
    if (url == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Chat endpoint allocation failed");
        return NULL;
    }
    memcpy(url, endpoint, endpoint_size);
    if (separator != 0) {
        url[endpoint_size] = '/';
    }
    memcpy(url + endpoint_size + separator, suffix, suffix_size + 1);
    return url;
}

bool telos_openai_chat_provider_dispatch(
    const struct telos_provider_request *request,
    telos_provider_event_fn emit,
    void *emit_context,
    void *provider_context,
    struct telos_error **error)
{
    telos_openai_chat_provider *provider = provider_context;
    struct telos_value *body_value = NULL;
    struct telos_openai_chat_sse_parser *parser = NULL;
    struct telos_secret_material *secret = NULL;
    char *body = NULL;
    char *url = NULL;
    size_t body_size;
    int status_code = 0;
    bool result = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (provider == NULL || request == NULL || emit == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Chat Provider dispatch arguments are invalid");
        return false;
    }
    body_value = telos_openai_chat_build_request(provider->model, request,
                                                 error);
    body_size = telos_value_json_size(body_value);
    if (body_value == NULL || body_size == 0) {
        goto cleanup;
    }
    body = malloc(body_size);
    if (body == NULL ||
        !telos_value_write_json(body_value, body, body_size, NULL, error)) {
        if (body == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Chat request serialization allocation failed");
        }
        goto cleanup;
    }
    url = endpoint_url(provider->endpoint, error);
    if (url == NULL) {
        goto cleanup;
    }
    secret = telos_secret_broker_resolve(
        provider->secret_broker, provider->secret_reference,
        provider->secret_target,
        (const char *const *)provider->capabilities, provider->capability_count,
        true, error);
    if (secret == NULL) {
        goto cleanup;
    }
    parser = telos_openai_chat_sse_parser_create(provider->unknown_event_policy,
                                                 emit, emit_context, error);
    if (parser == NULL) {
        goto cleanup;
    }
    {
        const struct telos_transport_request transport_request = {
            .method = "POST",
            .url = url,
            .content_type = "application/json",
            .accept = "text/event-stream",
            .bearer_token = telos_secret_material_data(secret),
            .headers = provider->headers,
            .header_count = provider->header_count,
            .body = body,
            .body_size = body_size - 1,
            .cancel = request->cancel,
        };

        if (!provider->send(&transport_request, receive_sse, parser,
                            &status_code, provider->transport_context, error)) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Chat transport failed");
            goto cleanup;
        }
    }
    if (status_code < 200 || status_code >= 300) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                  "Chat transport returned a non-success status");
        goto cleanup;
    }
    result = telos_openai_chat_sse_parser_finish(parser, error);

cleanup:
    telos_openai_chat_sse_parser_destroy(parser);
    telos_secret_material_destroy(secret);
    free(url);
    free(body);
    telos_value_release(body_value);
    return result;
}
