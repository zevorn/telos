#include <stdio.h>
#include <string.h>

#include <telos/plugins/openai_chat.h>

struct capture {
    enum telos_provider_event_kind kinds[16];
    size_t count;
    char text[64];
    char reasoning[64];
    char arguments[64];
    bool completed_arguments_valid;
    bool usage_seen;
};

static bool capture_event(const struct telos_provider_event *event,
                          void *context,
                          struct telos_error **error)
{
    struct capture *capture = context;

    (void)error;
    if (capture->count >= sizeof(capture->kinds) / sizeof(capture->kinds[0])) {
        return false;
    }
    capture->kinds[capture->count++] = event->kind;
    if (event->kind == TELOS_PROVIDER_TEXT_DELTA && event->delta != NULL) {
        strncat(capture->text, event->delta,
                sizeof(capture->text) - strlen(capture->text) - 1);
    }
    if (event->kind == TELOS_PROVIDER_REASONING_ITEM && event->delta != NULL) {
        strncat(capture->reasoning, event->delta,
                sizeof(capture->reasoning) - strlen(capture->reasoning) - 1);
    }
    if (event->kind == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA &&
        event->delta != NULL) {
        strncat(capture->arguments, event->delta,
                sizeof(capture->arguments) - strlen(capture->arguments) - 1);
    }
    if (event->kind == TELOS_PROVIDER_TOOL_CALL_COMPLETED) {
        capture->completed_arguments_valid =
            event->payload != NULL &&
            strcmp(telos_value_string(telos_value_get(event->payload, "text")),
                   "hello") == 0;
    }
    if (event->kind == TELOS_PROVIDER_USAGE_UPDATE) {
        capture->usage_seen = true;
    }
    return true;
}

int main(void)
{
    static const char stream[] =
        "data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"index\":0,"
        "\"delta\":{\"role\":\"assistant\",\"content\":\"hel\"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"index\":0,"
        "\"delta\":{\"content\":\"lo\",\"reasoning_content\":\"think\"},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"index\":0,"
        "\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call-1\","
        "\"function\":{\"name\":\"echo\",\"arguments\":\"{\\\"text\\\":\\\"\"}}]},"
        "\"finish_reason\":null}]}\n\n"
        "data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"index\":0,"
        "\"delta\":{\"tool_calls\":[{\"index\":0,"
        "\"function\":{\"arguments\":\"hello\\\"}\"}}]},"
        "\"finish_reason\":\"tool_calls\"}],"
        "\"usage\":{\"prompt_tokens\":2,\"completion_tokens\":3}}\n\n"
        "data: [DONE]\n\n";
    struct capture capture = {0};
    struct telos_openai_chat_sse_parser *parser =
        telos_openai_chat_sse_parser_create(
            TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR, capture_event, &capture,
            NULL);
    struct telos_error *error = NULL;
    bool passed = parser != NULL;

    for (size_t offset = 0; passed && offset < strlen(stream);) {
        size_t chunk = strlen(stream) - offset > 5 ? 5 : strlen(stream) - offset;

        passed = telos_openai_chat_sse_parser_feed(parser, stream + offset,
                                                   chunk, &error);
        offset += chunk;
    }
    passed = passed && telos_openai_chat_sse_parser_finish(parser, &error) &&
             capture.count == 10 &&
             capture.kinds[0] == TELOS_PROVIDER_RESPONSE_STARTED &&
             capture.kinds[1] == TELOS_PROVIDER_TEXT_DELTA &&
             capture.kinds[2] == TELOS_PROVIDER_TEXT_DELTA &&
             capture.kinds[3] == TELOS_PROVIDER_REASONING_ITEM &&
             capture.kinds[4] == TELOS_PROVIDER_TOOL_CALL_STARTED &&
             capture.kinds[5] == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA &&
             capture.kinds[6] == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA &&
             capture.kinds[7] == TELOS_PROVIDER_TOOL_CALL_COMPLETED &&
             capture.kinds[8] == TELOS_PROVIDER_USAGE_UPDATE &&
             capture.kinds[9] == TELOS_PROVIDER_RESPONSE_COMPLETED &&
             strcmp(capture.text, "hello") == 0 &&
             strcmp(capture.reasoning, "think") == 0 &&
             strcmp(capture.arguments, "{\"text\":\"hello\"}") == 0 &&
             capture.completed_arguments_valid && capture.usage_seen;

    telos_error_release(error);
    telos_openai_chat_sse_parser_destroy(parser);
    if (!passed) {
        fputs("Chat Completions SSE event mapping failed\n", stderr);
        return 1;
    }
    return 0;
}
