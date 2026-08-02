#include <stdio.h>
#include <string.h>

#include <telos/plugins/anthropic.h>

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
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{"
        "\"id\":\"msg_1\",\"usage\":{\"input_tokens\":2}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"hi\"}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tool-1\","
        "\"name\":\"echo\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\","
        "\"partial_json\":\"{\\\"text\\\":\\\"hello\\\"}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{"
        "\"stop_reason\":\"tool_use\"},\"usage\":{"
        "\"output_tokens\":3}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";
    struct capture capture = {0};
    struct telos_error *error = NULL;
    struct telos_anthropic_sse_parser *parser =
        telos_anthropic_sse_parser_create(
            TELOS_ANTHROPIC_UNKNOWN_EVENT_ERROR, capture_event, &capture,
            NULL);
    bool passed = parser != NULL;

    for (size_t offset = 0; passed && offset < strlen(stream);) {
        size_t chunk = strlen(stream) - offset > 7
                           ? 7
                           : strlen(stream) - offset;

        passed = telos_anthropic_sse_parser_feed(parser, stream + offset, chunk,
                                                 &error);
        offset += chunk;
    }
    passed = passed && telos_anthropic_sse_parser_finish(parser, &error) &&
             capture.count == 8 &&
             capture.kinds[0] == TELOS_PROVIDER_RESPONSE_STARTED &&
             capture.kinds[1] == TELOS_PROVIDER_USAGE_UPDATE &&
             capture.kinds[2] == TELOS_PROVIDER_TEXT_DELTA &&
             capture.kinds[3] == TELOS_PROVIDER_TOOL_CALL_STARTED &&
             capture.kinds[4] == TELOS_PROVIDER_TOOL_ARGUMENT_DELTA &&
             capture.kinds[5] == TELOS_PROVIDER_TOOL_CALL_COMPLETED &&
             capture.kinds[6] == TELOS_PROVIDER_USAGE_UPDATE &&
             capture.kinds[7] == TELOS_PROVIDER_RESPONSE_COMPLETED &&
             strcmp(capture.text, "hi") == 0 &&
             strcmp(capture.arguments, "{\"text\":\"hello\"}") == 0 &&
             capture.completed_arguments_valid && capture.usage_seen;

    telos_error_release(error);
    telos_anthropic_sse_parser_destroy(parser);
    if (!passed) {
        fputs("Anthropic SSE event mapping failed\n", stderr);
        return 1;
    }
    return 0;
}
