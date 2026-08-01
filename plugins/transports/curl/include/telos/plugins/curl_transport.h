#ifndef TELOS_PLUGINS_CURL_TRANSPORT_H
#define TELOS_PLUGINS_CURL_TRANSPORT_H

#include <telos/transport.h>
#include <telos/types.h>

#define TELOS_CURL_DEFAULT_TIMEOUT_MILLISECONDS 120000L

struct telos_curl_transport_config {
    long timeout_milliseconds;
};

bool telos_curl_transport_send(const telos_transport_request *request,
                               telos_transport_chunk_fn receive,
                               void *receive_context,
                               int *status_code,
                               void *transport_context,
                               struct telos_error **error);

#endif
