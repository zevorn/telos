#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/value.h>

#define TELOS_JSON_MAX_DEPTH 128

struct json_writer {
    char *buffer;
    size_t capacity;
    size_t position;
    bool valid;
};

struct json_parser {
    const char *current;
    const char *end;
    struct telos_error **error;
};

struct byte_buffer {
    char *data;
    size_t size;
    size_t capacity;
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

static void
writer_append(struct json_writer *writer, const char *data, size_t size)
{
    if (!writer->valid || size > SIZE_MAX - writer->position) {
        writer->valid = false;
        return;
    }

    if (writer->buffer != NULL) {
        if (writer->position > writer->capacity ||
            size > writer->capacity - writer->position) {
            writer->valid = false;
            return;
        }
        memcpy(writer->buffer + writer->position, data, size);
    }
    writer->position += size;
}

static void writer_character(struct json_writer *writer, char character)
{
    writer_append(writer, &character, 1);
}

static void write_string(struct json_writer *writer, const char *value)
{
    static const char hexadecimal[] = "0123456789abcdef";

    writer_character(writer, '"');
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != '\0'; ++cursor) {
        switch (*cursor) {
        case '"':
            writer_append(writer, "\\\"", 2);
            break;
        case '\\':
            writer_append(writer, "\\\\", 2);
            break;
        case '\b':
            writer_append(writer, "\\b", 2);
            break;
        case '\f':
            writer_append(writer, "\\f", 2);
            break;
        case '\n':
            writer_append(writer, "\\n", 2);
            break;
        case '\r':
            writer_append(writer, "\\r", 2);
            break;
        case '\t':
            writer_append(writer, "\\t", 2);
            break;
        default:
            if (*cursor < 0x20) {
                char escaped[] = {
                    '\\',
                    'u',
                    '0',
                    '0',
                    hexadecimal[*cursor >> 4],
                    hexadecimal[*cursor & 0x0f],
                };
                writer_append(writer, escaped, sizeof(escaped));
            } else {
                writer_character(writer, (char)*cursor);
            }
            break;
        }
    }
    writer_character(writer, '"');
}

static void write_value(struct json_writer *writer,
                        const struct telos_value *value,
                        unsigned int depth)
{
    char number[64];
    int length;
    size_t count;

    if (!writer->valid || value == NULL || depth > TELOS_JSON_MAX_DEPTH) {
        writer->valid = false;
        return;
    }

    switch (telos_value_type(value)) {
    case TELOS_VALUE_NULL:
        writer_append(writer, "null", 4);
        break;
    case TELOS_VALUE_BOOLEAN: {
        bool boolean = false;

        if (!telos_value_boolean(value, &boolean)) {
            writer->valid = false;
            return;
        }
        writer_append(writer, boolean ? "true" : "false", boolean ? 4 : 5);
        break;
    }
    case TELOS_VALUE_INTEGER: {
        int64_t integer = 0;

        if (!telos_value_integer(value, &integer)) {
            writer->valid = false;
            return;
        }
        length = snprintf(number, sizeof(number), "%" PRId64, integer);
        if (length < 0 || (size_t)length >= sizeof(number)) {
            writer->valid = false;
            return;
        }
        writer_append(writer, number, (size_t)length);
        break;
    }
    case TELOS_VALUE_REAL: {
        double real = 0.0;

        if (!telos_value_real(value, &real) || !isfinite(real)) {
            writer->valid = false;
            return;
        }
        length = snprintf(number, sizeof(number), "%.17g", real);
        if (length < 0 || (size_t)length >= sizeof(number)) {
            writer->valid = false;
            return;
        }
        writer_append(writer, number, (size_t)length);
        if (strchr(number, '.') == NULL && strchr(number, 'e') == NULL &&
            strchr(number, 'E') == NULL) {
            writer_append(writer, ".0", 2);
        }
        break;
    }
    case TELOS_VALUE_STRING:
        write_string(writer, telos_value_string(value));
        break;
    case TELOS_VALUE_ARRAY:
        count = telos_value_count(value);
        writer_character(writer, '[');
        for (size_t index = 0; index < count; ++index) {
            if (index > 0) {
                writer_character(writer, ',');
            }
            write_value(writer, telos_value_at(value, index), depth + 1);
        }
        writer_character(writer, ']');
        break;
    case TELOS_VALUE_OBJECT:
        count = telos_value_count(value);
        writer_character(writer, '{');
        for (size_t index = 0; index < count; ++index) {
            if (index > 0) {
                writer_character(writer, ',');
            }
            write_string(writer, telos_value_key_at(value, index));
            writer_character(writer, ':');
            write_value(
                writer,
                telos_value_get(value, telos_value_key_at(value, index)),
                depth + 1);
        }
        writer_character(writer, '}');
        break;
    case TELOS_VALUE_SENSITIVE:
        writer_append(writer, "{\"$redacted\":true}", 18);
        break;
    default:
        writer->valid = false;
        break;
    }
}

size_t telos_value_json_size(const struct telos_value *value)
{
    struct json_writer writer = {
        .buffer = NULL,
        .capacity = 0,
        .position = 0,
        .valid = true,
    };

    write_value(&writer, value, 0);
    if (!writer.valid || writer.position == SIZE_MAX) {
        return 0;
    }
    return writer.position + 1;
}

bool telos_value_write_json(const struct telos_value *value,
                            char *buffer,
                            size_t buffer_size,
                            size_t *written,
                            struct telos_error **error)
{
    struct json_writer writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .position = 0,
        .valid = true,
    };

    if (error != NULL) {
        *error = NULL;
    }
    if (value == NULL || buffer == NULL || buffer_size == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, 1,
                  "value, buffer, and non-zero buffer size are required");
        return false;
    }

    write_value(&writer, value, 0);
    if (!writer.valid || writer.position >= buffer_size) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, 2,
                  "JSON buffer is too small or Value is not serializable");
        return false;
    }

    buffer[writer.position] = '\0';
    if (written != NULL) {
        *written = writer.position;
    }
    return true;
}

static void parser_skip_whitespace(struct json_parser *parser)
{
    while (parser->current < parser->end &&
           isspace((unsigned char)*parser->current)) {
        ++parser->current;
    }
}

static bool byte_buffer_reserve(struct byte_buffer *buffer, size_t additional)
{
    size_t required;
    size_t capacity;
    char *new_data;

    if (additional > SIZE_MAX - buffer->size) {
        return false;
    }
    required = buffer->size + additional;
    if (required <= buffer->capacity) {
        return true;
    }

    capacity = buffer->capacity == 0 ? 32 : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }

    new_data = realloc(buffer->data, capacity);
    if (new_data == NULL) {
        return false;
    }
    buffer->data = new_data;
    buffer->capacity = capacity;
    return true;
}

static bool
byte_buffer_append(struct byte_buffer *buffer, const char *data, size_t size)
{
    if (!byte_buffer_reserve(buffer, size)) {
        return false;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return true;
}

static int hexadecimal_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool parser_code_unit(struct json_parser *parser, uint32_t *code_unit)
{
    uint32_t result = 0;

    if ((size_t)(parser->end - parser->current) < 4) {
        return false;
    }
    for (size_t index = 0; index < 4; ++index) {
        int digit = hexadecimal_value(parser->current[index]);

        if (digit < 0) {
            return false;
        }
        result = (result << 4) | (uint32_t)digit;
    }
    parser->current += 4;
    *code_unit = result;
    return true;
}

static bool append_code_point(struct byte_buffer *buffer, uint32_t code_point)
{
    char encoded[4];
    size_t size;

    if (code_point == 0 || code_point > 0x10ffff) {
        return false;
    }
    if (code_point <= 0x7f) {
        encoded[0] = (char)code_point;
        size = 1;
    } else if (code_point <= 0x7ff) {
        encoded[0] = (char)(0xc0 | (code_point >> 6));
        encoded[1] = (char)(0x80 | (code_point & 0x3f));
        size = 2;
    } else if (code_point <= 0xffff) {
        encoded[0] = (char)(0xe0 | (code_point >> 12));
        encoded[1] = (char)(0x80 | ((code_point >> 6) & 0x3f));
        encoded[2] = (char)(0x80 | (code_point & 0x3f));
        size = 3;
    } else {
        encoded[0] = (char)(0xf0 | (code_point >> 18));
        encoded[1] = (char)(0x80 | ((code_point >> 12) & 0x3f));
        encoded[2] = (char)(0x80 | ((code_point >> 6) & 0x3f));
        encoded[3] = (char)(0x80 | (code_point & 0x3f));
        size = 4;
    }
    return byte_buffer_append(buffer, encoded, size);
}

static char *parse_string(struct json_parser *parser)
{
    struct byte_buffer buffer = {0};

    if (parser->current >= parser->end || *parser->current != '"') {
        set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 1,
                  "expected a JSON string");
        return NULL;
    }
    ++parser->current;

    while (parser->current < parser->end) {
        unsigned char character = (unsigned char)*parser->current++;

        if (character == '"') {
            char terminator = '\0';

            if (!byte_buffer_append(&buffer, &terminator, 1)) {
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                          "could not allocate JSON string");
                free(buffer.data);
                return NULL;
            }
            return buffer.data;
        }
        if (character < 0x20) {
            set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 2,
                      "unescaped control character in JSON string");
            free(buffer.data);
            return NULL;
        }
        if (character != '\\') {
            char byte = (char)character;

            if (!byte_buffer_append(&buffer, &byte, 1)) {
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                          "could not allocate JSON string");
                free(buffer.data);
                return NULL;
            }
            continue;
        }

        if (parser->current >= parser->end) {
            break;
        }
        character = (unsigned char)*parser->current++;
        if (character == '"' || character == '\\' || character == '/') {
            char byte = (char)character;

            if (!byte_buffer_append(&buffer, &byte, 1)) {
                goto memory_failure;
            }
        } else if (character == 'b') {
            if (!byte_buffer_append(&buffer, "\b", 1)) {
                goto memory_failure;
            }
        } else if (character == 'f') {
            if (!byte_buffer_append(&buffer, "\f", 1)) {
                goto memory_failure;
            }
        } else if (character == 'n') {
            if (!byte_buffer_append(&buffer, "\n", 1)) {
                goto memory_failure;
            }
        } else if (character == 'r') {
            if (!byte_buffer_append(&buffer, "\r", 1)) {
                goto memory_failure;
            }
        } else if (character == 't') {
            if (!byte_buffer_append(&buffer, "\t", 1)) {
                goto memory_failure;
            }
        } else if (character == 'u') {
            uint32_t code_point;

            if (!parser_code_unit(parser, &code_point)) {
                goto syntax_failure;
            }
            if (code_point >= 0xd800 && code_point <= 0xdbff) {
                uint32_t low;

                if ((size_t)(parser->end - parser->current) < 6 ||
                    parser->current[0] != '\\' || parser->current[1] != 'u') {
                    goto syntax_failure;
                }
                parser->current += 2;
                if (!parser_code_unit(parser, &low) || low < 0xdc00 ||
                    low > 0xdfff) {
                    goto syntax_failure;
                }
                code_point =
                    0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
            } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
                goto syntax_failure;
            }
            if (!append_code_point(&buffer, code_point)) {
                goto syntax_failure;
            }
        } else {
            goto syntax_failure;
        }
    }

syntax_failure:
    set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 3,
              "invalid escape or unterminated JSON string");
    free(buffer.data);
    return NULL;

memory_failure:
    set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
              "could not allocate JSON string");
    free(buffer.data);
    return NULL;
}

static struct telos_value *parse_value(struct json_parser *parser,
                                       unsigned int depth);

static void release_values(struct telos_value **values, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        telos_value_release(values[index]);
    }
    free(values);
}

static struct telos_value *parse_array(struct json_parser *parser,
                                       unsigned int depth)
{
    struct telos_value **items = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct telos_value *result;

    ++parser->current;
    parser_skip_whitespace(parser);
    if (parser->current < parser->end && *parser->current == ']') {
        ++parser->current;
        return telos_value_new_array(NULL, 0);
    }

    while (parser->current < parser->end) {
        struct telos_value *item = parse_value(parser, depth + 1);

        if (item == NULL) {
            release_values(items, count);
            return NULL;
        }
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
            struct telos_value **new_items;

            if (new_capacity < capacity ||
                new_capacity > SIZE_MAX / sizeof(*new_items)) {
                telos_value_release(item);
                release_values(items, count);
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 2,
                          "JSON array is too large");
                return NULL;
            }
            new_items = realloc(items, new_capacity * sizeof(*new_items));
            if (new_items == NULL) {
                telos_value_release(item);
                release_values(items, count);
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                          "could not allocate JSON array");
                return NULL;
            }
            items = new_items;
            capacity = new_capacity;
        }
        items[count++] = item;
        parser_skip_whitespace(parser);

        if (parser->current < parser->end && *parser->current == ',') {
            ++parser->current;
            parser_skip_whitespace(parser);
            continue;
        }
        if (parser->current < parser->end && *parser->current == ']') {
            ++parser->current;
            result = telos_value_new_array(
                (const struct telos_value *const *)items, count);
            release_values(items, count);
            if (result == NULL) {
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                          "could not create JSON array");
            }
            return result;
        }
        break;
    }

    release_values(items, count);
    set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 4,
              "expected ',' or ']' in JSON array");
    return NULL;
}

static void
release_object_parts(char **keys, struct telos_value **values, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        free(keys[index]);
        telos_value_release(values[index]);
    }
    free(keys);
    free(values);
}

static bool
grow_object_parts(char ***keys, struct telos_value ***values, size_t *capacity)
{
    size_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
    char **new_keys;
    struct telos_value **new_values;

    if (new_capacity < *capacity ||
        new_capacity > SIZE_MAX / sizeof(*new_keys) ||
        new_capacity > SIZE_MAX / sizeof(*new_values)) {
        return false;
    }
    new_keys = realloc(*keys, new_capacity * sizeof(*new_keys));
    if (new_keys == NULL) {
        return false;
    }
    *keys = new_keys;

    new_values = realloc(*values, new_capacity * sizeof(*new_values));
    if (new_values == NULL) {
        return false;
    }
    *values = new_values;
    *capacity = new_capacity;
    return true;
}

static struct telos_value *parse_object(struct json_parser *parser,
                                        unsigned int depth)
{
    char **keys = NULL;
    struct telos_value **values = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct telos_value *result;

    ++parser->current;
    parser_skip_whitespace(parser);
    if (parser->current < parser->end && *parser->current == '}') {
        ++parser->current;
        return telos_value_new_object(NULL, NULL, 0);
    }

    while (parser->current < parser->end) {
        char *key = parse_string(parser);
        struct telos_value *value;

        if (key == NULL) {
            release_object_parts(keys, values, count);
            return NULL;
        }
        for (size_t index = 0; index < count; ++index) {
            if (strcmp(keys[index], key) == 0) {
                free(key);
                release_object_parts(keys, values, count);
                set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 5,
                          "duplicate key in JSON object");
                return NULL;
            }
        }

        parser_skip_whitespace(parser);
        if (parser->current >= parser->end || *parser->current != ':') {
            free(key);
            release_object_parts(keys, values, count);
            set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 6,
                      "expected ':' in JSON object");
            return NULL;
        }
        ++parser->current;
        parser_skip_whitespace(parser);
        value = parse_value(parser, depth + 1);
        if (value == NULL) {
            free(key);
            release_object_parts(keys, values, count);
            return NULL;
        }

        if (count == capacity &&
            !grow_object_parts(&keys, &values, &capacity)) {
            free(key);
            telos_value_release(value);
            release_object_parts(keys, values, count);
            set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                      "could not allocate JSON object");
            return NULL;
        }
        keys[count] = key;
        values[count] = value;
        ++count;
        parser_skip_whitespace(parser);

        if (parser->current < parser->end && *parser->current == ',') {
            ++parser->current;
            parser_skip_whitespace(parser);
            continue;
        }
        if (parser->current < parser->end && *parser->current == '}') {
            ++parser->current;
            result = telos_value_new_object(
                (const char *const *)keys,
                (const struct telos_value *const *)values, count);
            release_object_parts(keys, values, count);
            if (result == NULL) {
                set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                          "could not create JSON object");
            }
            return result;
        }
        break;
    }

    release_object_parts(keys, values, count);
    set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 7,
              "expected ',' or '}' in JSON object");
    return NULL;
}

static bool parser_literal(struct json_parser *parser, const char *literal)
{
    size_t size = strlen(literal);

    if ((size_t)(parser->end - parser->current) < size ||
        memcmp(parser->current, literal, size) != 0) {
        return false;
    }
    parser->current += size;
    return true;
}

static struct telos_value *parse_number(struct json_parser *parser)
{
    const char *start = parser->current;
    bool real = false;
    size_t size;
    char *text;
    char *number_end;
    struct telos_value *result;

    if (*parser->current == '-') {
        ++parser->current;
    }
    if (parser->current >= parser->end) {
        goto invalid;
    }
    if (*parser->current == '0') {
        ++parser->current;
        if (parser->current < parser->end &&
            isdigit((unsigned char)*parser->current)) {
            goto invalid;
        }
    } else if (*parser->current >= '1' && *parser->current <= '9') {
        do {
            ++parser->current;
        } while (parser->current < parser->end &&
                 isdigit((unsigned char)*parser->current));
    } else {
        goto invalid;
    }

    if (parser->current < parser->end && *parser->current == '.') {
        real = true;
        ++parser->current;
        if (parser->current >= parser->end ||
            !isdigit((unsigned char)*parser->current)) {
            goto invalid;
        }
        while (parser->current < parser->end &&
               isdigit((unsigned char)*parser->current)) {
            ++parser->current;
        }
    }
    if (parser->current < parser->end &&
        (*parser->current == 'e' || *parser->current == 'E')) {
        real = true;
        ++parser->current;
        if (parser->current < parser->end &&
            (*parser->current == '+' || *parser->current == '-')) {
            ++parser->current;
        }
        if (parser->current >= parser->end ||
            !isdigit((unsigned char)*parser->current)) {
            goto invalid;
        }
        while (parser->current < parser->end &&
               isdigit((unsigned char)*parser->current)) {
            ++parser->current;
        }
    }

    size = (size_t)(parser->current - start);
    text = malloc(size + 1);
    if (text == NULL) {
        set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                  "could not allocate JSON number");
        return NULL;
    }
    memcpy(text, start, size);
    text[size] = '\0';
    errno = 0;
    if (real) {
        double value = strtod(text, &number_end);

        result = errno == 0 && number_end == text + size && isfinite(value)
                     ? telos_value_new_real(value)
                     : NULL;
    } else {
        intmax_t value = strtoimax(text, &number_end, 10);

        result = errno == 0 && number_end == text + size &&
                         value >= INT64_MIN && value <= INT64_MAX
                     ? telos_value_new_integer((int64_t)value)
                     : NULL;
    }
    free(text);
    if (result == NULL) {
        set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 8,
                  "JSON number is out of range");
    }
    return result;

invalid:
    set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 9,
              "invalid JSON number");
    return NULL;
}

static struct telos_value *parse_value(struct json_parser *parser,
                                       unsigned int depth)
{
    char *string;
    struct telos_value *result;

    if (depth > TELOS_JSON_MAX_DEPTH) {
        set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 10,
                  "JSON nesting limit exceeded");
        return NULL;
    }
    parser_skip_whitespace(parser);
    if (parser->current >= parser->end) {
        set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 11,
                  "expected a JSON value");
        return NULL;
    }

    switch (*parser->current) {
    case 'n':
        if (parser_literal(parser, "null")) {
            return telos_value_new_null();
        }
        break;
    case 't':
        if (parser_literal(parser, "true")) {
            return telos_value_new_boolean(true);
        }
        break;
    case 'f':
        if (parser_literal(parser, "false")) {
            return telos_value_new_boolean(false);
        }
        break;
    case '"':
        string = parse_string(parser);
        if (string == NULL) {
            return NULL;
        }
        result = telos_value_new_string(string);
        free(string);
        if (result == NULL) {
            set_error(parser->error, TELOS_ERROR_DOMAIN_MEMORY, 1,
                      "could not create JSON string");
        }
        return result;
    case '[':
        return parse_array(parser, depth);
    case '{':
        return parse_object(parser, depth);
    default:
        if (*parser->current == '-' ||
            isdigit((unsigned char)*parser->current)) {
            return parse_number(parser);
        }
        break;
    }

    set_error(parser->error, TELOS_ERROR_DOMAIN_PROTOCOL, 12,
              "invalid JSON value");
    return NULL;
}

struct telos_value *telos_value_parse_json(const char *json,
                                           size_t size,
                                           struct telos_error **error)
{
    struct json_parser parser;
    struct telos_value *result;

    if (error != NULL) {
        *error = NULL;
    }
    if (json == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, 1,
                  "JSON input is required");
        return NULL;
    }

    parser.current = json;
    parser.end = json + size;
    parser.error = error;
    result = parse_value(&parser, 0);
    if (result == NULL) {
        return NULL;
    }

    parser_skip_whitespace(&parser);
    if (parser.current != parser.end) {
        telos_value_release(result);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, 13,
                  "unexpected data after JSON value");
        return NULL;
    }
    return result;
}
