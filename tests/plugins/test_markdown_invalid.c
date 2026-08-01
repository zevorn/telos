#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/plugins/markdown_store.h>

#define HEADER "# Telos Event Log v1\n\n"
#define FENCE "```json telos-event\n"
#define END "\n```\n<!-- telos:event:end -->\n\n"
#define ID "\"00000000000000000000000000000001\""

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    bool result = file != NULL && fwrite(text, 1, size, file) == size;

    if (file != NULL) {
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool write_record(FILE *file, uint64_t sequence)
{
    return fprintf(file,
                   "## %" PRIu64 " · event\n\n" FENCE "{\"sequence\":%" PRIu64
                   ","
                   "\"event_id\":" ID ","
                   "\"session_id\":" ID ","
                   "\"correlation_id\":" ID ","
                   "\"causation_id\":" ID ","
                   "\"type\":\"event\","
                   "\"source\":\"test\","
                   "\"timestamp_milliseconds\":0,"
                   "\"payload\":null}" END,
                   sequence, sequence) > 0;
}

static bool rejects_content(const char *content)
{
    char path[] = "/tmp/telos-markdown-invalid-XXXXXX";
    int descriptor = mkstemp(path);
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    bool rejected;

    if (descriptor < 0) {
        return false;
    }
    close(descriptor);
    if (!write_text(path, content)) {
        unlink(path);
        return false;
    }
    store = telos_markdown_store_create(path, &error);
    rejected = store == NULL && error != NULL;
    telos_event_store_destroy(store);
    telos_error_release(error);
    unlink(path);
    return rejected;
}

static bool rejects_json(const char *json)
{
    size_t size =
        strlen(HEADER) + strlen(FENCE) + strlen(json) + strlen(END) + 32;
    char *content = malloc(size);
    bool rejected;

    if (content == NULL) {
        return false;
    }
    snprintf(content, size, HEADER "## record\n\n" FENCE "%s" END, json);
    rejected = rejects_content(content);
    free(content);
    return rejected;
}

static bool rejects_fields(const char *sequence,
                           const char *timestamp,
                           const char *event_id,
                           const char *session_id,
                           const char *correlation_id,
                           const char *causation_id,
                           const char *type,
                           const char *source)
{
    char json[2048];

    snprintf(json, sizeof(json),
             "{\"sequence\":%s,"
             "\"event_id\":%s,"
             "\"session_id\":%s,"
             "\"correlation_id\":%s,"
             "\"causation_id\":%s,"
             "\"type\":%s,"
             "\"source\":%s,"
             "\"timestamp_milliseconds\":%s,"
             "\"payload\":null}",
             sequence, event_id, session_id, correlation_id, causation_id, type,
             source, timestamp);
    return rejects_json(json);
}

static struct telos_event *
new_event(uint64_t sequence, const char *type, const char *source)
{
    struct telos_value *payload = telos_value_new_null();
    const struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = type,
        .source = source,
        .timestamp_milliseconds = 0,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);

    telos_value_release(payload);
    return event;
}

static bool recovery_matrix(void)
{
    char path[] = "/tmp/telos-markdown-recovery-XXXXXX";
    char duplicate_path[] = "/tmp/telos-markdown-duplicate-XXXXXX";
    int descriptor = mkstemp(path);
    FILE *file;
    struct telos_event_store *store;
    bool passed = descriptor >= 0;

    if (!passed) {
        return false;
    }
    file = fdopen(descriptor, "wb");
    passed = file != NULL &&
             fwrite(HEADER, 1, strlen(HEADER), file) == strlen(HEADER);
    for (uint64_t sequence = 1; passed && sequence <= 9; ++sequence) {
        passed = write_record(file, sequence);
    }
    if (file != NULL) {
        passed = fclose(file) == 0 && passed;
    }
    store = telos_markdown_store_create(path, NULL);
    {
        struct telos_event *last = telos_event_store_get(store, 8, NULL);

        passed = passed && store != NULL &&
                 telos_event_store_count(store) == 9 && last != NULL &&
                 telos_event_sequence(last) == 9;
        telos_event_release(last);
    }
    telos_event_store_destroy(store);
    unlink(path);

    descriptor = mkstemp(duplicate_path);
    if (descriptor < 0) {
        return false;
    }
    file = fdopen(descriptor, "wb");
    if (file == NULL) {
        close(descriptor);
        unlink(duplicate_path);
        return false;
    }
    passed = fwrite(HEADER, 1, strlen(HEADER), file) == strlen(HEADER) &&
             write_record(file, 1) && write_record(file, 1) && passed;
    passed = fclose(file) == 0 && passed;
    {
        struct telos_error *error = NULL;

        store = telos_markdown_store_create(duplicate_path, &error);
        passed = passed && store == NULL && error != NULL;
        telos_error_release(error);
    }
    unlink(duplicate_path);
    return passed;
}

static bool append_matrix(void)
{
    char path[] = "/tmp/telos-markdown-append-XXXXXX";
    int descriptor = mkstemp(path);
    struct telos_event_store *store;
    struct telos_event *safe;
    struct telos_event *duplicate;
    struct telos_event *overflow;
    struct telos_error *error = NULL;
    bool passed;

    if (descriptor < 0) {
        return false;
    }
    close(descriptor);
    unlink(path);
    store = telos_markdown_store_create(path, NULL);
    safe = new_event(1, "type`\n", "source`\t");
    duplicate = new_event(1, "duplicate", "test");
    overflow = new_event(UINT64_MAX, "overflow", "test");
    passed = store != NULL && safe != NULL && duplicate != NULL &&
             overflow != NULL && telos_event_store_append(store, safe, NULL) &&
             !telos_event_store_append(store, duplicate, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_event_store_append(store, overflow, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && telos_event_store_get(store, 1, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    telos_event_release(overflow);
    telos_event_release(duplicate);
    telos_event_release(safe);
    telos_event_store_destroy(store);
    unlink(path);
    return passed;
}

int main(void)
{
    char directory[] = "/tmp/telos-markdown-directory-XXXXXX";
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    bool passed = mkdtemp(directory) != NULL;

    store = telos_markdown_store_create(directory, &error);
    passed = passed && store == NULL && error != NULL;
    telos_error_release(error);
    rmdir(directory);

    passed =
        passed && rejects_content("wrong header\n") &&
        rejects_content(HEADER "x") && rejects_content(HEADER "## record") &&
        rejects_content(HEADER "## record\n" FENCE "{}") &&
        rejects_json("{invalid}") && rejects_json("[]") && rejects_json("{}") &&
        rejects_fields("0", "0", ID, ID, ID, ID, "\"event\"", "\"test\"") &&
        rejects_fields("\"one\"", "0", ID, ID, ID, ID, "\"event\"",
                       "\"test\"") &&
        rejects_fields("1", "null", ID, ID, ID, ID, "\"event\"", "\"test\"") &&
        rejects_fields("1", "0", "null", ID, ID, ID, "\"event\"", "\"test\"") &&
        rejects_fields("1", "0", ID, "null", ID, ID, "\"event\"", "\"test\"") &&
        rejects_fields("1", "0", ID, ID, "null", ID, "\"event\"", "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, "null", "\"event\"", "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, ID, "null", "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, ID, "\"event\"", "null") &&
        rejects_fields("1", "0", "\"bad\"", ID, ID, ID, "\"event\"",
                       "\"test\"") &&
        rejects_fields("1", "0", ID, "\"bad\"", ID, ID, "\"event\"",
                       "\"test\"") &&
        rejects_fields("1", "0", ID, ID, "\"bad\"", ID, "\"event\"",
                       "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, "\"bad\"", "\"event\"",
                       "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, ID, "\"\"", "\"test\"") &&
        rejects_fields("1", "0", ID, ID, ID, ID, "\"event\"", "\"\"") &&
        rejects_json("{\"sequence\":1,"
                     "\"event_id\":" ID ","
                     "\"session_id\":" ID ","
                     "\"correlation_id\":" ID ","
                     "\"causation_id\":" ID ","
                     "\"type\":\"event\","
                     "\"source\":\"test\","
                     "\"timestamp_milliseconds\":0}") &&
        recovery_matrix() && append_matrix();

    if (!passed) {
        fputs("Markdown Store corruption matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
