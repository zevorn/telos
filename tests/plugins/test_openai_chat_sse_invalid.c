#include <stdio.h>
#include <string.h>

#include <telos/plugins/openai_chat.h>

static bool accept_event(const struct telos_provider_event *event,
                         void *context,
                         struct telos_error **error)
{
    (void)event;
    (void)context;
    (void)error;
    return true;
}

static bool rejects(const char *stream)
{
    struct telos_openai_chat_sse_parser *parser =
        telos_openai_chat_sse_parser_create(
            TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR, accept_event, NULL, NULL);
    struct telos_error *error = NULL;
    bool result = parser != NULL &&
                  telos_openai_chat_sse_parser_feed(parser, stream,
                                                    strlen(stream), &error);

    if (result) {
        result = telos_openai_chat_sse_parser_finish(parser, &error);
    }
    telos_openai_chat_sse_parser_destroy(parser);
    result = !result && error != NULL;
    telos_error_release(error);
    return result;
}

int main(void)
{
    static const char invalid_json[] = "data: {bad}\n\n";
    static const char scalar[] = "data: []\n\n";
    static const char no_id[] =
        "data: {\"choices\":[{\"delta\":{\"content\":\"x\"}}]}\n\n";
    static const char bad_tool[] =
        "data: {\"id\":\"x\",\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,\"function\":{\"arguments\":\"[]\"}}]},"
        "\"finish_reason\":\"tool_calls\"}]}\n\n";
    static const char partial[] =
        "data: {\"id\":\"x\",\"choices\":[{\"delta\":{}}]";
    bool passed = rejects(invalid_json) && rejects(scalar) && rejects(no_id) &&
                  rejects(bad_tool) && rejects(partial);

    if (!passed) {
        fputs("Chat Completions SSE validation failed\n", stderr);
        return 1;
    }
    return 0;
}
