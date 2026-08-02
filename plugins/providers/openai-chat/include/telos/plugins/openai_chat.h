#ifndef TELOS_PLUGINS_OPENAI_CHAT_H
#define TELOS_PLUGINS_OPENAI_CHAT_H

#include <telos/provider.h>
#include <telos/secret.h>
#include <telos/transport.h>
#include <telos/types.h>

enum telos_openai_chat_unknown_event_policy {
    TELOS_OPENAI_CHAT_UNKNOWN_EVENT_IGNORE = 1,
    TELOS_OPENAI_CHAT_UNKNOWN_EVENT_ERROR,
};

typedef enum telos_openai_chat_unknown_event_policy
    telos_openai_chat_unknown_event_policy;
typedef telos_openai_chat_unknown_event_policy telos_chat_policy;

struct telos_openai_chat_sse_parser;
struct telos_openai_chat_provider;

struct telos_openai_chat_config {
    const char *model;
    const char *endpoint;
    const char *secret_reference;
    const char *secret_target;
    struct telos_secret_broker *secret_broker;
    const char *const *capabilities;
    size_t capability_count;
    const struct telos_transport_header *headers;
    size_t header_count;
    telos_transport_send_fn send;
    void *transport_context;
    enum telos_openai_chat_unknown_event_policy unknown_event_policy;
};

typedef struct telos_openai_chat_provider telos_openai_chat_provider;
typedef struct telos_openai_chat_sse_parser telos_openai_chat_sse_parser;
typedef struct telos_openai_chat_config telos_openai_chat_config;
typedef telos_provider_request telos_req;

telos_openai_chat_provider *
telos_openai_chat_provider_create(const telos_openai_chat_config *config,
                                  struct telos_error **error);

void telos_openai_chat_provider_destroy(telos_openai_chat_provider *provider);

bool telos_openai_chat_provider_dispatch(const telos_req *request,
                                         telos_provider_event_fn emit,
                                         void *emit_context,
                                         void *provider_context,
                                         struct telos_error **error);

struct telos_value *telos_openai_chat_build_request(const char *model,
                                                    const telos_req *request,
                                                    struct telos_error **error);

telos_openai_chat_sse_parser *
telos_openai_chat_sse_parser_create(telos_chat_policy unknown_policy,
                                    telos_provider_event_fn callback,
                                    void *callback_context,
                                    struct telos_error **error);

void telos_openai_chat_sse_parser_destroy(telos_openai_chat_sse_parser *parser);

bool telos_openai_chat_sse_parser_feed(telos_openai_chat_sse_parser *parser,
                                       const char *data,
                                       size_t size,
                                       struct telos_error **error);

bool telos_openai_chat_sse_parser_finish(telos_openai_chat_sse_parser *parser,
                                         struct telos_error **error);

#endif
