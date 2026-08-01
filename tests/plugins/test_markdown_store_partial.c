#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <telos/plugins/markdown_store.h>

int main(void)
{
    char path[] = "/tmp/telos-markdown-partial-XXXXXX";
    int descriptor = mkstemp(path);
    FILE *file;
    struct telos_error *error = NULL;
    struct telos_event_store *store;

    if (descriptor < 0) {
        return 1;
    }
    file = fdopen(descriptor, "wb");
    if (file == NULL) {
        close(descriptor);
        unlink(path);
        return 1;
    }
    static const char partial_record[] =
        "# Telos Event Log v1\n\n## 1 · incomplete\n";

    if (fwrite(partial_record, 1, strlen(partial_record), file) !=
        strlen(partial_record)) {
        fclose(file);
        unlink(path);
        return 1;
    }
    if (fclose(file) != 0) {
        unlink(path);
        return 1;
    }

    store = telos_markdown_store_create(path, &error);
    if (store != NULL || error == NULL ||
        telos_error_domain(error) != TELOS_ERROR_DOMAIN_PROTOCOL) {
        fputs("partial Markdown record was not detected\n", stderr);
        telos_event_store_destroy(store);
        telos_error_release(error);
        unlink(path);
        return 1;
    }

    telos_error_release(error);
    unlink(path);
    return 0;
}
