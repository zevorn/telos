#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugins/jsonl_store.h>
#include <telos/store_plugin.h>

#define JSONL_MAXIMUM_RECORD_BYTES (1024U * 1024U)
#define JSONL_MAXIMUM_FILE_BYTES (16U * 1024U * 1024U)
#define JSONL_INITIAL_EVENT_CAPACITY 16U

struct jsonl_store {
    struct telos_event_store base;
    FILE *file;
    size_t count;
    size_t capacity;
    struct telos_event **events;
};

static struct telos_value *event_envelope(const struct telos_event *event)
{
    char event_id[TELOS_ID_TEXT_SIZE];
    char session_id[TELOS_ID_TEXT_SIZE];
    char correlation_id[TELOS_ID_TEXT_SIZE];
    char causation_id[TELOS_ID_TEXT_SIZE];
    const char *keys[] = {
        "sequence", "event_id", "session_id", "correlation_id",
        "causation_id", "type", "source", "timestamp_milliseconds",
        "payload",
    };
    struct telos_value *values[9] = {0};
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
    values[7] = telos_value_new_integer(
        telos_event_timestamp_milliseconds(event));
    values[8] = telos_value_retain(telos_event_payload(event));
    for (size_t index = 0; index < 9; ++index) {
        if (values[index] == NULL) {
            goto cleanup;
        }
    }
    result = telos_value_new_object(
        keys, (const struct telos_value *const *)values, 9);

cleanup:
    for (size_t index = 0; index < 9; ++index) {
        telos_value_release(values[index]);
    }
    return result;
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
    struct telos_event_spec spec = {0};
    const struct telos_value *payload;
    const char *event_id;
    const char *session_id;
    const char *correlation_id;
    const char *causation_id;
    const char *type;
    const char *source;
    int64_t sequence;
    int64_t timestamp;
    struct telos_event *event;

    if (envelope == NULL || telos_value_type(envelope) != TELOS_VALUE_OBJECT) {
        if (envelope != NULL) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                                  "JSONL Event envelope is not an object");
        }
        telos_value_release(envelope);
        return NULL;
    }
    event_id = value_string_field(envelope, "event_id");
    session_id = value_string_field(envelope, "session_id");
    correlation_id = value_string_field(envelope, "correlation_id");
    causation_id = value_string_field(envelope, "causation_id");
    type = value_string_field(envelope, "type");
    source = value_string_field(envelope, "source");
    payload = telos_value_get(envelope, "payload");
    if (!value_integer_field(envelope, "sequence", &sequence) ||
        sequence <= 0 ||
        !value_integer_field(envelope, "timestamp_milliseconds", &timestamp) ||
        event_id == NULL || session_id == NULL || correlation_id == NULL ||
        causation_id == NULL || type == NULL || source == NULL ||
        payload == NULL || !telos_id_parse(event_id, &spec.event_id) ||
        !telos_id_parse(session_id, &spec.session_id) ||
        !telos_id_parse(correlation_id, &spec.correlation_id) ||
        !telos_id_parse(causation_id, &spec.causation_id)) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                              "JSONL Event envelope is invalid");
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

static bool reserve_events(struct jsonl_store *jsonl,
                           struct telos_error **error)
{
    struct telos_event **events;
    size_t capacity;

    if (jsonl->count < jsonl->capacity) {
        return true;
    }
    capacity = jsonl->capacity == 0
                   ? JSONL_INITIAL_EVENT_CAPACITY
                   : jsonl->capacity * 2;
    if (capacity < jsonl->capacity ||
        capacity > JSONL_MAXIMUM_FILE_BYTES / sizeof(*events)) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "JSONL Store capacity overflow");
        return false;
    }
    events = realloc(jsonl->events, capacity * sizeof(*events));
    if (events == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "JSONL Store allocation failed");
        return false;
    }
    jsonl->events = events;
    jsonl->capacity = capacity;
    return true;
}

static bool append_owned_event(struct jsonl_store *jsonl,
                               const struct telos_event *event,
                               struct telos_error **error)
{
    if (jsonl->count > 0 &&
        telos_event_sequence(event) <=
            telos_event_sequence(jsonl->events[jsonl->count - 1])) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_STATE, EALREADY,
                              "Event sequence is not strictly increasing");
        return false;
    }
    if (!reserve_events(jsonl, error)) {
        return false;
    }
    jsonl->events[jsonl->count++] = telos_event_retain(event);
    return true;
}

static bool append_record(struct jsonl_store *jsonl,
                          const struct telos_event *event,
                          struct telos_error **error)
{
    struct telos_value *envelope = event_envelope(event);
    char *json = NULL;
    size_t size;
    bool result = false;

    if (envelope == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EOVERFLOW,
                              "Event cannot be represented by JSONL Store");
        return false;
    }
    size = telos_value_json_size(envelope);
    if (size == 0 || size > JSONL_MAXIMUM_RECORD_BYTES) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                              "JSONL Event record is too large");
        goto cleanup;
    }
    json = malloc(size);
    if (json == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "JSONL Event record allocation failed");
        goto cleanup;
    }
    if (!telos_value_write_json(envelope, json, size, NULL, error) ||
        fwrite(json, 1, size - 1, jsonl->file) != size - 1 ||
        fputc('\n', jsonl->file) == EOF || fflush(jsonl->file) != 0) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "JSONL Store append failed");
        goto cleanup;
    }
    result = true;

cleanup:
    free(json);
    telos_value_release(envelope);
    return result;
}

static bool append_loaded_event(struct jsonl_store *jsonl,
                                struct telos_event *event,
                                struct telos_error **error)
{
    if (!append_owned_event(jsonl, event, error)) {
        return false;
    }
    telos_event_release(event);
    return true;
}

static bool load_content(struct jsonl_store *jsonl, char *content, size_t size,
                         struct telos_error **error)
{
    char *cursor = content;
    char *end = content + size;

    while (cursor < end) {
        char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_size = line_end == NULL
                               ? (size_t)(end - cursor)
                               : (size_t)(line_end - cursor);
        struct telos_event *event;

        if (line_size > 0 && cursor[line_size - 1] == '\r') {
            --line_size;
        }
        if (line_size == 0) {
            cursor = line_end == NULL ? end : line_end + 1;
            continue;
        }
        if (line_size > JSONL_MAXIMUM_RECORD_BYTES) {
            telos_store_set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EFBIG,
                                  "JSONL Store record is too large");
            return false;
        }
        event = parse_envelope(cursor, line_size, error);
        if (event == NULL || !append_loaded_event(jsonl, event, error)) {
            telos_event_release(event);
            return false;
        }
        cursor = line_end == NULL ? end : line_end + 1;
    }
    return true;
}

static bool load_file(struct jsonl_store *jsonl, const char *path,
                      struct telos_error **error)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *content;
    bool result;

    if (file == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "JSONL Store could not be opened for reading");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "JSONL Store size could not be read");
        return false;
    }
    length = ftell(file);
    if (length < 0 || (size_t)length > JSONL_MAXIMUM_FILE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        telos_store_set_error(
            error, length >= 0 && (size_t)length > JSONL_MAXIMUM_FILE_BYTES
                       ? TELOS_ERROR_DOMAIN_PROTOCOL
                       : TELOS_ERROR_DOMAIN_IO,
            length >= 0 && (size_t)length > JSONL_MAXIMUM_FILE_BYTES ? EFBIG
                                                                      : EIO,
            "JSONL Store size could not be read");
        return false;
    }
    if (length == 0) {
        fclose(file);
        return true;
    }
    content = malloc((size_t)length);
    if (content == NULL || fread(content, 1, (size_t)length, file) !=
                             (size_t)length ||
        fclose(file) != 0) {
        free(content);
        telos_store_set_error(error, content == NULL ? TELOS_ERROR_DOMAIN_MEMORY
                                                      : TELOS_ERROR_DOMAIN_IO,
                              content == NULL ? ENOMEM : EIO,
                              "JSONL Store content could not be read");
        return false;
    }
    result = load_content(jsonl, content, (size_t)length, error);
    free(content);
    return result;
}

static void jsonl_destroy(struct telos_event_store *store)
{
    struct jsonl_store *jsonl = (struct jsonl_store *)store;

    if (jsonl->file != NULL) {
        fclose(jsonl->file);
    }
    for (size_t index = 0; index < jsonl->count; ++index) {
        telos_event_release(jsonl->events[index]);
    }
    free(jsonl->events);
    free(jsonl);
}

static bool jsonl_append(struct telos_event_store *store,
                         const struct telos_event *event,
                         struct telos_error **error)
{
    struct jsonl_store *jsonl = (struct jsonl_store *)store;

    if (event == NULL || telos_event_sequence(event) == 0) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                              "JSONL Event is invalid");
        return false;
    }
    if (jsonl->count > 0 &&
        telos_event_sequence(event) <=
            telos_event_sequence(jsonl->events[jsonl->count - 1])) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_STATE, EALREADY,
                              "Event sequence is not strictly increasing");
        return false;
    }
    if (!reserve_events(jsonl, error) || !append_record(jsonl, event, error)) {
        return false;
    }
    jsonl->events[jsonl->count++] = telos_event_retain(event);
    return true;
}

static size_t jsonl_count(const struct telos_event_store *store)
{
    const struct jsonl_store *jsonl = (const struct jsonl_store *)store;

    return jsonl->count;
}

static struct telos_event *jsonl_get(const struct telos_event_store *store,
                                     size_t index,
                                     struct telos_error **error)
{
    const struct jsonl_store *jsonl = (const struct jsonl_store *)store;

    if (index >= jsonl->count) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ERANGE,
                              "JSONL Store index is out of range");
        return NULL;
    }
    return telos_event_retain(jsonl->events[index]);
}

static const struct telos_event_store_ops jsonl_ops = {
    .destroy = jsonl_destroy,
    .append = jsonl_append,
    .count = jsonl_count,
    .get = jsonl_get,
};

struct telos_event_store *
telos_jsonl_store_create(const char *path, struct telos_error **error)
{
    struct jsonl_store *jsonl;
    FILE *initial;

    if (error != NULL) {
        *error = NULL;
    }
    if (path == NULL || path[0] == '\0') {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                              "JSONL Store path is required");
        return NULL;
    }
    initial = fopen(path, "ab");
    if (initial == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "JSONL Store could not be created");
        return NULL;
    }
    fclose(initial);
    jsonl = calloc(1, sizeof(*jsonl));
    if (jsonl == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                              "JSONL Store allocation failed");
        return NULL;
    }
    jsonl->base.ops = &jsonl_ops;
    if (!load_file(jsonl, path, error)) {
        jsonl_destroy(&jsonl->base);
        return NULL;
    }
    jsonl->file = fopen(path, "ab");
    if (jsonl->file == NULL) {
        telos_store_set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                              "JSONL Store could not be opened for append");
        jsonl_destroy(&jsonl->base);
        return NULL;
    }
    return &jsonl->base;
}
