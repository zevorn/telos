#include <stdio.h>
#include <string.h>

#include <telos/openai_responses.h>

static bool accept_event(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
)
{
    (void)event;
    (void)context;
    (void)error;
    return true;
}

static bool rejects(const char *stream, size_t size, bool finish)
{
    struct telos_openai_sse_parser *parser =
        telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            accept_event,
            NULL,
            NULL
        );
    struct telos_error *error = NULL;
    bool result = telos_openai_sse_parser_feed(
        parser,
        stream,
        size,
        &error
    );

    if (result && finish) {
        result = telos_openai_sse_parser_finish(parser, &error);
    }
    telos_openai_sse_parser_destroy(parser);
    if (result || error == NULL) {
        telos_error_release(error);
        return false;
    }
    telos_error_release(error);
    return true;
}

int main(void)
{
    static const char unknown[] =
        "event: response.future\n"
        "data: {\"type\":\"response.future\"}\n\n";
    static const char mismatch[] =
        "event: response.function_call_arguments.delta\n"
        "data: {\"type\":\"response.function_call_arguments.delta\","
        "\"item_id\":\"missing\",\"delta\":\"{}\"}\n\n";
    static const char partial[] =
        "event: response.created\n"
        "data: {\"type\":\"response.created\"}";
    static const char invalid_utf8[] = {
        'e', 'v', 'e', 'n', 't', ':', ' ', 'x', '\n',
        'd', 'a', 't', 'a', ':', ' ', '{', '"', 't', 'y', 'p', 'e', '"',
        ':', '"', 'x', '"', ',', '"', 'v', '"', ':', (char)0xc0, (char)0xaf,
        '}', '\n', '\n',
    };
    struct telos_openai_sse_parser *ignore =
        telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_IGNORE,
            accept_event,
            NULL,
            NULL
        );
    bool ignored = telos_openai_sse_parser_feed(
        ignore,
        unknown,
        strlen(unknown),
        NULL
    ) && telos_openai_sse_parser_finish(ignore, NULL);

    telos_openai_sse_parser_destroy(ignore);
    if (
        !ignored
        || !rejects(unknown, strlen(unknown), true)
        || !rejects(mismatch, strlen(mismatch), true)
        || !rejects(partial, strlen(partial), true)
        || !rejects(invalid_utf8, sizeof(invalid_utf8), true)
    ) {
        fputs("Responses SSE compatibility or validation policy failed\n", stderr);
        return 1;
    }
    return 0;
}
