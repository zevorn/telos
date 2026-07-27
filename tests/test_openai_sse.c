#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/openai_responses.h>

struct capture {
    enum telos_provider_event_kind kinds[16];
    size_t count;
    char arguments[64];
    bool completed_arguments_valid;
};

static bool capture_event(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
)
{
    struct capture *capture = context;

    (void)error;
    if (capture->count >= 16) {
        return false;
    }
    capture->kinds[capture->count++] = event->kind;
    if (
        event->kind == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA
        && event->delta != NULL
    ) {
        strncat(
            capture->arguments,
            event->delta,
            sizeof(capture->arguments) - strlen(capture->arguments) - 1
        );
    }
    if (event->kind == TELOS_PROVIDER_TOOL_CALL_COMPLETED) {
        const char *text = telos_value_string(
            telos_value_get(event->payload, "text")
        );

        capture->completed_arguments_valid = text != NULL
            && strcmp(text, "hello") == 0;
    }
    return true;
}

static char *read_fixture(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *content;

    if (
        file == NULL
        || fseek(file, 0, SEEK_END) != 0
        || (length = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0
    ) {
        return NULL;
    }
    content = malloc((size_t)length);
    if (
        content == NULL
        || fread(content, 1, (size_t)length, file) != (size_t)length
    ) {
        free(content);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return content;
}

int main(int argc, char **argv)
{
    struct capture capture = {0};
    struct telos_openai_sse_parser *parser =
        telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            capture_event,
            &capture,
            NULL
        );
    size_t size;
    char *fixture;
    bool passed;

    if (argc != 2 || parser == NULL) {
        return 1;
    }
    fixture = read_fixture(argv[1], &size);
    passed = fixture != NULL;
    for (size_t offset = 0; passed && offset < size;) {
        size_t chunk = size - offset > 7 ? 7 : size - offset;

        passed = telos_openai_sse_parser_feed(
            parser,
            fixture + offset,
            chunk,
            NULL
        );
        offset += chunk;
    }
    passed = passed
        && telos_openai_sse_parser_finish(parser, NULL)
        && capture.count == 10
        && capture.kinds[0] == TELOS_PROVIDER_RESPONSE_STARTED
        && capture.kinds[1] == TELOS_PROVIDER_OUTPUT_ITEM_ADDED
        && capture.kinds[2] == TELOS_PROVIDER_TOOL_CALL_STARTED
        && capture.kinds[3] == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA
        && capture.kinds[4] == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA
        && capture.kinds[5] == TELOS_PROVIDER_TOOL_CALL_COMPLETED
        && capture.kinds[6] == TELOS_PROVIDER_TEXT_DELTA
        && capture.kinds[7] == TELOS_PROVIDER_REASONING_ITEM
        && capture.kinds[8] == TELOS_PROVIDER_USAGE_UPDATE
        && capture.kinds[9] == TELOS_PROVIDER_RESPONSE_COMPLETED
        && capture.completed_arguments_valid
        && strcmp(capture.arguments, "{\"text\":\"hello\"}") == 0;

    free(fixture);
    telos_openai_sse_parser_destroy(parser);
    if (!passed) {
        fputs("Responses SSE did not preserve typed event order\n", stderr);
        return 1;
    }
    return 0;
}
