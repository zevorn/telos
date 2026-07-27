#ifndef TELOS_OPENAI_RESPONSES_H
#define TELOS_OPENAI_RESPONSES_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/provider.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_openai_unknown_event_policy {
    TELOS_OPENAI_UNKNOWN_EVENT_IGNORE = 1,
    TELOS_OPENAI_UNKNOWN_EVENT_ERROR,
};

struct telos_openai_sse_parser;

struct telos_value *telos_openai_responses_build_request(
    const char *model,
    const struct telos_provider_request *request,
    struct telos_error **error
);

struct telos_openai_sse_parser *telos_openai_sse_parser_create(
    enum telos_openai_unknown_event_policy unknown_policy,
    telos_provider_event_fn callback,
    void *callback_context,
    struct telos_error **error
);

void telos_openai_sse_parser_destroy(
    struct telos_openai_sse_parser *parser
);

bool telos_openai_sse_parser_feed(
    struct telos_openai_sse_parser *parser,
    const char *data,
    size_t size,
    struct telos_error **error
);

bool telos_openai_sse_parser_finish(
    struct telos_openai_sse_parser *parser,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
