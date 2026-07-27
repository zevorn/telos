#ifndef TELOS_TRANSPORT_H
#define TELOS_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_transport_request {
    const char *method;
    const char *url;
    const char *content_type;
    const char *bearer_token;
    const char *body;
    size_t body_size;
};

typedef bool (*telos_transport_chunk_fn)(
    const char *data,
    size_t size,
    void *context,
    struct telos_error **error
);

typedef bool (*telos_transport_send_fn)(
    const struct telos_transport_request *request,
    telos_transport_chunk_fn receive,
    void *receive_context,
    int *status_code,
    void *transport_context,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
