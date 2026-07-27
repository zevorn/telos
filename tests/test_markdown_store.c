#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <telos/store.h>

static struct telos_event *new_event(void)
{
    struct telos_value *visible = telos_value_new_string("hello");
    struct telos_value *sensitive = telos_value_new_sensitive(
        "never-write-this-token"
    );
    const char *keys[] = {"output", "credential"};
    const struct telos_value *values[] = {visible, sensitive};
    struct telos_value *payload = telos_value_new_object(keys, values, 2);
    const struct telos_event_spec spec = {
        .sequence = 42,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = "tool.completed",
        .source = "plugin:com.example.echo",
        .timestamp_milliseconds = 123456789,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);

    telos_value_release(payload);
    telos_value_release(sensitive);
    telos_value_release(visible);
    return event;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *content;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    content = malloc((size_t)length + 1);
    if (
        content == NULL
        || fread(content, 1, (size_t)length, file) != (size_t)length
    ) {
        free(content);
        fclose(file);
        return NULL;
    }
    content[length] = '\0';
    fclose(file);
    return content;
}

int main(void)
{
    char path[] = "/tmp/telos-markdown-store-XXXXXX";
    int descriptor = mkstemp(path);
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    struct telos_event *event = new_event();
    struct telos_event *recovered;
    char *content;

    if (descriptor < 0) {
        fputs("could not allocate temporary Store path\n", stderr);
        telos_event_release(event);
        return 1;
    }
    close(descriptor);
    unlink(path);

    store = telos_markdown_store_create(path, &error);
    if (
        store == NULL
        || error != NULL
        || !telos_event_store_append(store, event, &error)
        || error != NULL
    ) {
        fputs("could not create or append Markdown Store\n", stderr);
        telos_error_release(error);
        telos_event_store_destroy(store);
        telos_event_release(event);
        unlink(path);
        return 1;
    }
    telos_event_store_destroy(store);

    content = read_file(path);
    if (
        content == NULL
        || strstr(content, "## 42 · tool.completed") == NULL
        || strstr(content, "\"output\":\"hello\"") == NULL
        || strstr(content, "never-write-this-token") != NULL
        || strstr(content, "{\"$redacted\":true}") == NULL
    ) {
        fputs("Markdown Store output is not readable or safely redacted\n", stderr);
        free(content);
        telos_event_release(event);
        unlink(path);
        return 1;
    }
    free(content);

    store = telos_markdown_store_create(path, &error);
    recovered = telos_event_store_get(store, 0, &error);
    if (
        store == NULL
        || error != NULL
        || telos_event_store_count(store) != 1
        || recovered == NULL
        || telos_event_sequence(recovered) != 42
        || !telos_id_equal(telos_event_id(recovered), telos_event_id(event))
        || !telos_id_equal(
            telos_event_session_id(recovered),
            telos_event_session_id(event)
        )
        || strcmp(telos_event_type(recovered), "tool.completed") != 0
        || strcmp(
            telos_event_source(recovered),
            "plugin:com.example.echo"
        ) != 0
    ) {
        fputs("Markdown Store did not recover the Event\n", stderr);
        telos_error_release(error);
        telos_event_release(recovered);
        telos_event_store_destroy(store);
        telos_event_release(event);
        unlink(path);
        return 1;
    }

    telos_event_release(recovered);
    telos_event_store_destroy(store);
    telos_event_release(event);
    unlink(path);
    return 0;
}
