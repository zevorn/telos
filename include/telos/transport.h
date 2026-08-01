#ifndef TELOS_TRANSPORT_H
#define TELOS_TRANSPORT_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/error.h>

struct telos_transport_request {
    const char *method;
    const char *url;
    const char *content_type;
    const char *bearer_token;
    const char *body;
    size_t body_size;
    const struct telos_cancel *cancel;
};

typedef struct telos_transport_request telos_transport_request;

typedef bool (*telos_transport_chunk_fn)(const char *data,
                                         size_t size,
                                         void *context,
                                         struct telos_error **error);

typedef bool (*telos_transport_send_fn)(const telos_transport_request *request,
                                        telos_transport_chunk_fn receive,
                                        void *receive_context,
                                        int *status_code,
                                        void *transport_context,
                                        struct telos_error **error);

struct telos_transport_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_transport_send_fn send;
};

#endif
