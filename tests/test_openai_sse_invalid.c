#include <stdio.h>
#include <stdlib.h>
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

static bool reject_event(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
)
{
    (void)event;
    (void)context;
    (void)error;
    return false;
}

static bool run(
    const char *stream,
    size_t size,
    bool finish,
    enum telos_openai_unknown_event_policy policy,
    telos_provider_event_fn callback,
    bool expected
)
{
    struct telos_openai_sse_parser *parser =
        telos_openai_sse_parser_create(
            policy,
            callback,
            NULL,
            NULL
        );
    struct telos_error *error = NULL;
    bool result = parser != NULL
        && telos_openai_sse_parser_feed(parser, stream, size, &error);

    if (result && finish) {
        result = telos_openai_sse_parser_finish(parser, &error);
    }
    telos_openai_sse_parser_destroy(parser);
    result = result == expected
        && (expected || error != NULL);
    telos_error_release(error);
    return result;
}

static bool rejects(const char *stream, size_t size, bool finish)
{
    return run(
        stream,
        size,
        finish,
        TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
        accept_event,
        false
    );
}

static bool accepts(const char *stream)
{
    return run(
        stream,
        strlen(stream),
        true,
        TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
        accept_event,
        true
    );
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
    static const char no_type[] =
        "data: {\"response\":{\"id\":\"response\"}}\n\n";
    static const char mismatch_type[] =
        "event: response.completed\n"
        "data: {\"type\":\"response.created\","
        "\"response\":{\"id\":\"response\"}}\n\n";
    static const char invalid_field[] =
        "id: one\n"
        "data: {\"type\":\"response.failed\"}\n\n";
    static const char no_data[] =
        "event: response.failed\n\n";
    static const char invalid_json[] =
        "data: {invalid}\n\n";
    static const char scalar_json[] =
        "data: []\n\n";
    static const char created_no_response[] =
        "data: {\"type\":\"response.created\"}\n\n";
    static const char created_no_id[] =
        "data: {\"type\":\"response.created\",\"response\":{}}\n\n";
    static const char added_no_id[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"type\":\"message\"}}\n\n";
    static const char added_no_type[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\"}}\n\n";
    static const char call_no_name[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\"}}\n\n";
    static const char call_no_call_id[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"name\":\"tool\"}}\n\n";
    static const char duplicate_call[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n"
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call-2\",\"name\":\"tool\"}}\n\n";
    static const char delta_no_value[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\","
        "\"item_id\":\"item\"}\n\n";
    static const char done_unknown[] =
        "data: {\"type\":\"response.output_item.done\","
        "\"item\":{\"id\":\"missing\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\",\"arguments\":\"{}\"}}\n\n";
    static const char done_mismatch[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n"
        "data: {\"type\":\"response.output_item.done\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"other\",\"name\":\"tool\",\"arguments\":\"{}\"}}\n\n";
    static const char done_no_arguments[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n"
        "data: {\"type\":\"response.output_item.done\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n";
    static const char done_bad_json[] =
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\"}}\n\n"
        "data: {\"type\":\"response.output_item.done\","
        "\"item\":{\"id\":\"item\",\"type\":\"function_call\","
        "\"call_id\":\"call\",\"name\":\"tool\",\"arguments\":\"[]\"}}\n\n";
    static const char text_no_delta[] =
        "data: {\"type\":\"response.output_text.delta\"}\n\n";
    static const char completed_no_id[] =
        "data: {\"type\":\"response.completed\",\"response\":{}}\n\n";
    static const char valid_variants[] =
        ": comment\r\n"
        "event: response.output_item.added\r\n"
        "data: {\"type\":\"response.output_item.added\","
        "\"item\":{\"id\":\"message\",\"type\":\"message\"}}\r\n\r\n"
        "data: {\"type\":\"response.output_item.done\","
        "\"item\":{\"type\":\"message\"}}\n\n"
        "data: {\"type\":\"response.reasoning_text.delta\","
        "\"item_id\":\"reasoning\",\"delta\":\"part\"}\n\n"
        "data: {\"type\":\"response.failed\"}\n\n"
        "data: {\"type\":\"error\"}\n\n"
        "data: {\"type\":\"response.completed\","
        "\"response\":{\"id\":\"response\"}}\n\n"
        "data: [DONE]\n\n";
    static const char multiline_data[] =
        "event: response.failed\n"
        "data: {\"type\":\n"
        "data: \"response.failed\"}\n\n";
    static const char valid_utf8[] =
        "data: {\"type\":\"response.failed\","
        "\"text\":\"\xc2\xa2\xe2\x82\xac\xf0\x90\x8d\x88\"}\n\n";
    static const char invalid_utf8[] = {
        'e', 'v', 'e', 'n', 't', ':', ' ', 'x', '\n',
        'd', 'a', 't', 'a', ':', ' ', '{', '"', 't', 'y', 'p', 'e', '"',
        ':', '"', 'x', '"', ',', '"', 'v', '"', ':', (char)0xc0, (char)0xaf,
        '}', '\n', '\n',
    };
    static const char truncated_utf8[] = {
        'd', 'a', 't', 'a', ':', ' ', '{', '"', 't', 'y', 'p', 'e', '"',
        ':', '"', 'x', '"', ',', '"', 'v', '"', ':', '"', (char)0xe2,
        (char)0x82, '"', '}', '\n', '\n',
    };
    static const char bad_continuation[] = {
        'd', 'a', 't', 'a', ':', ' ', '{', '"', 't', 'y', 'p', 'e', '"',
        ':', '"', 'x', '"', ',', '"', 'v', '"', ':', '"', (char)0xe2,
        'x', (char)0xac, '"', '}', '\n', '\n',
    };
    static const char surrogate_utf8[] = {
        'd', 'a', 't', 'a', ':', ' ', '{', '"', 't', 'y', 'p', 'e', '"',
        ':', '"', 'x', '"', ',', '"', 'v', '"', ':', '"', (char)0xed,
        (char)0xa0, (char)0x80, '"', '}', '\n', '\n',
    };
    struct telos_error *error = NULL;
    struct telos_openai_sse_parser *parser;
    char *large = malloc(1024U * 1024U + 1);
    bool passed = large != NULL
        && run(
            unknown,
            strlen(unknown),
            true,
            TELOS_OPENAI_UNKNOWN_EVENT_IGNORE,
            accept_event,
            true
        )
        && rejects(unknown, strlen(unknown), true)
        && rejects(mismatch, strlen(mismatch), true)
        && rejects(partial, strlen(partial), true)
        && rejects(no_type, strlen(no_type), true)
        && rejects(mismatch_type, strlen(mismatch_type), true)
        && rejects(invalid_field, strlen(invalid_field), true)
        && rejects(no_data, strlen(no_data), true)
        && rejects(invalid_json, strlen(invalid_json), true)
        && rejects(scalar_json, strlen(scalar_json), true)
        && rejects(created_no_response, strlen(created_no_response), true)
        && rejects(created_no_id, strlen(created_no_id), true)
        && rejects(added_no_id, strlen(added_no_id), true)
        && rejects(added_no_type, strlen(added_no_type), true)
        && rejects(call_no_name, strlen(call_no_name), true)
        && rejects(call_no_call_id, strlen(call_no_call_id), true)
        && rejects(duplicate_call, strlen(duplicate_call), true)
        && rejects(delta_no_value, strlen(delta_no_value), true)
        && rejects(done_unknown, strlen(done_unknown), true)
        && rejects(done_mismatch, strlen(done_mismatch), true)
        && rejects(done_no_arguments, strlen(done_no_arguments), true)
        && rejects(done_bad_json, strlen(done_bad_json), true)
        && rejects(text_no_delta, strlen(text_no_delta), true)
        && rejects(completed_no_id, strlen(completed_no_id), true)
        && rejects(invalid_utf8, sizeof(invalid_utf8), true)
        && rejects(truncated_utf8, sizeof(truncated_utf8), true)
        && rejects(bad_continuation, sizeof(bad_continuation), true)
        && rejects(surrogate_utf8, sizeof(surrogate_utf8), true)
        && accepts(valid_variants)
        && accepts(multiline_data)
        && accepts(valid_utf8)
        && run(
            "data: {\"type\":\"response.failed\"}\n\n",
            strlen("data: {\"type\":\"response.failed\"}\n\n"),
            true,
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            reject_event,
            false
        );

    passed = passed
        && telos_openai_sse_parser_create(0, accept_event, NULL, &error)
            == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            NULL,
            NULL,
            &error
        ) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    telos_openai_sse_parser_destroy(NULL);

    parser = telos_openai_sse_parser_create(
        TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
        accept_event,
        NULL,
        NULL
    );
    passed = passed
        && !telos_openai_sse_parser_feed(NULL, "", 0, &error)
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !telos_openai_sse_parser_feed(parser, NULL, 1, &error)
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_openai_sse_parser_feed(parser, NULL, 0, NULL)
        && telos_openai_sse_parser_feed(parser, " \r\n\t", 4, NULL)
        && telos_openai_sse_parser_finish(parser, NULL)
        && !telos_openai_sse_parser_finish(parser, &error)
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && !telos_openai_sse_parser_feed(parser, "", 0, &error)
        && error != NULL;
    telos_error_release(error);
    telos_openai_sse_parser_destroy(parser);

    if (large == NULL) {
        passed = false;
    } else {
        memset(large, 'x', 1024U * 1024U + 1);
        parser = telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            accept_event,
            NULL,
            NULL
        );
        passed = !telos_openai_sse_parser_feed(
            parser,
            large,
            1024U * 1024U + 1,
            &error
        )
            && error != NULL
            && passed;
        telos_error_release(error);
        error = NULL;
        telos_openai_sse_parser_destroy(parser);

        parser = telos_openai_sse_parser_create(
            TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
            accept_event,
            NULL,
            NULL
        );
        passed = telos_openai_sse_parser_feed(
            parser,
            large,
            600U * 1024U,
            NULL
        )
            && !telos_openai_sse_parser_feed(
                parser,
                large,
                500U * 1024U,
                &error
            )
            && error != NULL
            && passed;
        telos_error_release(error);
        telos_openai_sse_parser_destroy(parser);
    }
    free(large);

    if (!passed) {
        fputs("Responses SSE compatibility or validation policy failed\n", stderr);
        return 1;
    }
    return 0;
}
