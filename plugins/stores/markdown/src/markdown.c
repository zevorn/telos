#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/markdown_store.h>
#include <telos/store_plugin.h>

#define MARKDOWN_HEADER "# Telos Event Log v1\n\n"
#define RECORD_FENCE "```json telos-event\n"
#define RECORD_END "\n```\n<!-- telos:event:end -->\n\n"

struct markdown_store {
    struct telos_event_store base;
    FILE *file;
    size_t count;
    size_t capacity;
    struct telos_event **events;
};

static char *copy_safe_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);

    if (copy == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < size; ++index) {
        const unsigned char character = (unsigned char)text[index];

        copy[index] =
            character < 0x20 && character != '\0' ? ' ' : (char)character;
        if (copy[index] == '`') {
            copy[index] = '\'';
        }
    }
    return copy;
}

static bool append_owned_event(struct markdown_store *markdown,
                               const struct telos_event *event,
                               struct telos_error **error)
{
    struct telos_event **events;
    size_t capacity;

    if (markdown->count > 0 &&
        telos_event_sequence(event) <=
            telos_event_sequence(markdown->events[markdown->count - 1])) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_STATE, EALREADY,
                              "Event sequence is not strictly increasing");
        return false;
    }

    if (markdown->count == markdown->capacity) {
        capacity = markdown->capacity == 0 ? 8 : markdown->capacity * 2;
        if (capacity < markdown->capacity ||
            capacity > SIZE_MAX / sizeof(*events)) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                                  "Markdown Store capacity overflow");
            return false;
        }
        events = realloc(markdown->events, capacity * sizeof(*events));
        if (events == NULL) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                                  "Markdown Store allocation failed");
            return false;
        }
        markdown->events = events;
        markdown->capacity = capacity;
    }

    markdown->events[markdown->count++] = telos_event_retain(event);
    return true;
}

static struct telos_value *event_envelope(const struct telos_event *event)
{
    char event_id[TELOS_ID_TEXT_SIZE];
    char session_id[TELOS_ID_TEXT_SIZE];
    char correlation_id[TELOS_ID_TEXT_SIZE];
    char causation_id[TELOS_ID_TEXT_SIZE];
    struct telos_value *values[9] = {0};
    const char *keys[] = {
        "sequence",     "event_id", "session_id", "correlation_id",
        "causation_id", "type",     "source",     "timestamp_milliseconds",
        "payload",
    };
    struct telos_value *result = NULL;
    const uint64_t sequence = telos_event_sequence(event);

    if (sequence > INT64_MAX ||
        !telos_id_format(telos_event_id(event), event_id, sizeof(event_id)) ||
        !telos_id_format(telos_event_session_id(event), session_id,
                         sizeof(session_id)) ||
        !telos_id_format(telos_event_correlation_id(event), correlation_id,
                         sizeof(correlation_id)) ||
        !telos_id_format(telos_event_causation_id(event), causation_id,
                         sizeof(causation_id))) {
        return NULL;
    }

    values[0] = telos_value_new_integer((int64_t)sequence);
    values[1] = telos_value_new_string(event_id);
    values[2] = telos_value_new_string(session_id);
    values[3] = telos_value_new_string(correlation_id);
    values[4] = telos_value_new_string(causation_id);
    values[5] = telos_value_new_string(telos_event_type(event));
    values[6] = telos_value_new_string(telos_event_source(event));
    values[7] =
        telos_value_new_integer(telos_event_timestamp_milliseconds(event));
    values[8] = telos_value_retain(telos_event_payload(event));

    for (size_t index = 0; index < 9; ++index) {
        if (values[index] == NULL) {
            goto cleanup;
        }
    }
    result =
        telos_value_new_object(keys, (const struct telos_value *const *)values,
                               9);

cleanup:
    for (size_t index = 0; index < 9; ++index) {
        telos_value_release(values[index]);
    }
    return result;
}

static char *format_record(const struct telos_event *event, size_t *record_size,
                           struct telos_error **error)
{
    struct telos_value *envelope = event_envelope(event);
    size_t json_size;
    char *json = NULL;
    char *type = NULL;
    char *source = NULL;
    char event_id[TELOS_ID_TEXT_SIZE];
    char session_id[TELOS_ID_TEXT_SIZE];
    char correlation_id[TELOS_ID_TEXT_SIZE];
    char causation_id[TELOS_ID_TEXT_SIZE];
    int length;
    char *record = NULL;

    if (envelope == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EOVERFLOW,
                              "Event cannot be represented by Markdown Store");
        return NULL;
    }
    json_size = telos_value_json_size(envelope);
    json = malloc(json_size);
    type = copy_safe_text(telos_event_type(event));
    source = copy_safe_text(telos_event_source(event));
    if (json == NULL || type == NULL || source == NULL ||
        !telos_value_write_json(envelope, json, json_size, NULL, error) ||
        !telos_id_format(telos_event_id(event), event_id, sizeof(event_id)) ||
        !telos_id_format(telos_event_session_id(event), session_id,
                         sizeof(session_id)) ||
        !telos_id_format(telos_event_correlation_id(event), correlation_id,
                         sizeof(correlation_id)) ||
        !telos_id_format(telos_event_causation_id(event), causation_id,
                         sizeof(causation_id))) {
        if (error == NULL || *error == NULL) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                                  "Markdown record allocation failed");
        }
        goto cleanup;
    }

    length =
        snprintf(NULL, 0,
                 "## %" PRIu64 " · %s\n\n"
                 "- Event: `%s`\n"
                 "- Session: `%s`\n"
                 "- Correlation: `%s`\n"
                 "- Causation: `%s`\n"
                 "- Source: `%s`\n"
                 "- Timestamp: `%" PRId64 "`\n\n" RECORD_FENCE "%s" RECORD_END,
                 telos_event_sequence(event), type, event_id, session_id,
                 correlation_id, causation_id, source,
                 telos_event_timestamp_milliseconds(event), json);
    if (length < 0) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Markdown record formatting failed");
        goto cleanup;
    }
    record = malloc((size_t)length + 1);
    if (record == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "Markdown record allocation failed");
        goto cleanup;
    }
    if (snprintf(record, (size_t)length + 1,
                 "## %" PRIu64 " · %s\n\n"
                 "- Event: `%s`\n"
                 "- Session: `%s`\n"
                 "- Correlation: `%s`\n"
                 "- Causation: `%s`\n"
                 "- Source: `%s`\n"
                 "- Timestamp: `%" PRId64 "`\n\n" RECORD_FENCE "%s" RECORD_END,
                 telos_event_sequence(event), type, event_id, session_id,
                 correlation_id, causation_id, source,
                 telos_event_timestamp_milliseconds(event), json) != length) {
        free(record);
        record = NULL;
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Markdown record formatting changed");
        goto cleanup;
    }
    *record_size = (size_t)length;

cleanup:
    free(source);
    free(type);
    free(json);
    telos_value_release(envelope);
    return record;
}

static bool value_integer_field(const struct telos_value *object,
                                const char *key, int64_t *result)
{
    return telos_value_integer(telos_value_get(object, key), result);
}

static const char *value_string_field(const struct telos_value *object,
                                      const char *key)
{
    return telos_value_string(telos_value_get(object, key));
}

static struct telos_event *parse_envelope(const char *json, size_t size,
                                          struct telos_error **error)
{
    struct telos_value *envelope = telos_value_parse_json(json, size, error);
    int64_t sequence;
    int64_t timestamp;
    const char *event_id;
    const char *session_id;
    const char *correlation_id;
    const char *causation_id;
    const char *type;
    const char *source;
    const struct telos_value *payload;
    struct telos_event_spec spec = {0};
    struct telos_event *event = NULL;

    if (envelope != NULL) {
        event_id = value_string_field(envelope, "event_id");
        session_id = value_string_field(envelope, "session_id");
        correlation_id = value_string_field(envelope, "correlation_id");
        causation_id = value_string_field(envelope, "causation_id");
        type = value_string_field(envelope, "type");
        source = value_string_field(envelope, "source");
        payload = telos_value_get(envelope, "payload");
    }
    if (envelope == NULL || telos_value_type(envelope) != TELOS_VALUE_OBJECT ||
        !value_integer_field(envelope, "sequence", &sequence) ||
        sequence <= 0 ||
        !value_integer_field(envelope, "timestamp_milliseconds", &timestamp) ||
        event_id == NULL || session_id == NULL || correlation_id == NULL ||
        causation_id == NULL || type == NULL || source == NULL ||
        payload == NULL ||
        !telos_id_parse(event_id, &spec.event_id) ||
        !telos_id_parse(session_id, &spec.session_id) ||
        !telos_id_parse(correlation_id, &spec.correlation_id) ||
        !telos_id_parse(causation_id, &spec.causation_id)) {
        if (envelope != NULL && (error == NULL || *error == NULL)) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                                  "Markdown Event envelope is invalid");
        }
        telos_value_release(envelope);
        return NULL;
    }

    spec.sequence = (uint64_t)sequence;
    spec.type = type;
    spec.source = source;
    spec.timestamp_milliseconds = timestamp;
    spec.payload = payload;
    event = telos_event_create(&spec, error);
    telos_value_release(envelope);
    return event;
}

static bool whitespace_only(const char *start, const char *end)
{
    for (const char *cursor = start; cursor < end; ++cursor) {
        if (*cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
            *cursor != '\n') {
            return false;
        }
    }
    return true;
}

static bool recover_records(struct markdown_store *markdown, char *content,
                            size_t size, struct telos_error **error)
{
    const size_t header_size = strlen(MARKDOWN_HEADER);
    char *cursor;
    char *end = content + size;

    if (size < header_size ||
        memcmp(content, MARKDOWN_HEADER, header_size) != 0) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                              "Markdown Store header is invalid");
        return false;
    }
    cursor = content + header_size;
    while (cursor < end) {
        char *fence;
        char *record_end;
        struct telos_event *event;

        if (whitespace_only(cursor, end)) {
            return true;
        }
        if ((size_t)(end - cursor) < 3 || memcmp(cursor, "## ", 3) != 0) {
            goto partial;
        }
        fence = strstr(cursor, RECORD_FENCE);
        if (fence == NULL) {
            goto partial;
        }
        fence += strlen(RECORD_FENCE);
        record_end = strstr(fence, RECORD_END);
        if (record_end == NULL) {
            goto partial;
        }

        event = parse_envelope(fence, (size_t)(record_end - fence), error);
        if (event == NULL) {
            return false;
        }
        if (!append_owned_event(markdown, event, error)) {
            telos_event_release(event);
            return false;
        }
        telos_event_release(event);
        cursor = record_end + strlen(RECORD_END);
    }
    return true;

partial:
    telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                          "Markdown Store has a partial trailing record");
    return false;
}

static bool load_file(struct markdown_store *markdown, const char *path,
                      struct telos_error **error)
{
    FILE *file = fopen(path, "rb");
    long length = -1;
    char *content;
    bool result;

    if (file == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "Markdown Store could not be opened for reading");
        return false;
    }
    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
    }
    if (length < 0) {
        fclose(file);
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Markdown Store size could not be read");
        return false;
    }
    if (length == 0) {
        fclose(file);
        return true;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Markdown Store could not be rewound");
        return false;
    }
    content = calloc((size_t)length + 1, 1);
    if (content == NULL ||
        fread(content, 1, (size_t)length, file) != (size_t)length) {
        free(content);
        fclose(file);
        telos_store_set_error(error,
                              content == NULL ? TELOS_ERROR_DOMAIN_MEMORY
                                              : TELOS_ERROR_DOMAIN_IO,
                              content == NULL ? ENOMEM : EIO,
                              "Markdown Store content could not be read");
        return false;
    }
    fclose(file);
    result = recover_records(markdown, content, (size_t)length, error);
    free(content);
    return result;
}

static void markdown_destroy(struct telos_event_store *store)
{
    struct markdown_store *markdown = (struct markdown_store *)store;

    if (markdown->file != NULL) {
        fclose(markdown->file);
    }
    for (size_t index = 0; index < markdown->count; ++index) {
        telos_event_release(markdown->events[index]);
    }
    free(markdown->events);
    free(markdown);
}

static bool markdown_append(struct telos_event_store *store,
                            const struct telos_event *event,
                            struct telos_error **error)
{
    struct markdown_store *markdown = (struct markdown_store *)store;
    size_t record_size = 0;
    char *record;

    if (markdown->count > 0 &&
        telos_event_sequence(event) <=
            telos_event_sequence(markdown->events[markdown->count - 1])) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_STATE, EALREADY,
                              "Event sequence is not strictly increasing");
        return false;
    }

    record = format_record(event, &record_size, error);
    if (record == NULL) {
        return false;
    }
    if (fwrite(record, 1, record_size, markdown->file) != record_size ||
        fflush(markdown->file) != 0) {
        free(record);
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Markdown Store append failed");
        return false;
    }
    free(record);
    return append_owned_event(markdown, event, error);
}

static size_t markdown_count(const struct telos_event_store *store)
{
    const struct markdown_store *markdown =
        (const struct markdown_store *)store;

    return markdown->count;
}

static struct telos_event *markdown_get(const struct telos_event_store *store,
                                        size_t index,
                                        struct telos_error **error)
{
    const struct markdown_store *markdown =
        (const struct markdown_store *)store;

    if (index >= markdown->count) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ERANGE,
                              "Markdown Store index is out of range");
        return NULL;
    }
    return telos_event_retain(markdown->events[index]);
}

static const struct telos_event_store_ops markdown_ops = {
    .destroy = markdown_destroy,
    .append = markdown_append,
    .count = markdown_count,
    .get = markdown_get,
};

struct telos_event_store *
telos_markdown_store_create(const char *path, struct telos_error **error)
{
    struct markdown_store *markdown;
    FILE *initial;

    if (error != NULL) {
        *error = NULL;
    }
    if (path == NULL || path[0] == '\0') {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                              "Markdown Store path is required");
        return NULL;
    }

    initial = fopen(path, "ab");
    if (initial == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "Markdown Store could not be created");
        return NULL;
    }
    fclose(initial);

    markdown = calloc(1, sizeof(*markdown));
    if (markdown == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "Markdown Store allocation failed");
        return NULL;
    }
    markdown->base.ops = &markdown_ops;
    if (!load_file(markdown, path, error)) {
        markdown_destroy(&markdown->base);
        return NULL;
    }

    markdown->file = fopen(path, "ab");
    if (markdown->file == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "Markdown Store could not be opened for append");
        markdown_destroy(&markdown->base);
        return NULL;
    }
    if (markdown->count == 0 && fseek(markdown->file, 0, SEEK_END) == 0 &&
        ftell(markdown->file) == 0) {
        if (fwrite(MARKDOWN_HEADER, 1, strlen(MARKDOWN_HEADER),
                   markdown->file) != strlen(MARKDOWN_HEADER) ||
            fflush(markdown->file) != 0) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                                  "Markdown Store header write failed");
            markdown_destroy(&markdown->base);
            return NULL;
        }
    }
    return &markdown->base;
}
