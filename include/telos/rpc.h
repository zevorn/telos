#ifndef TELOS_RPC_H
#define TELOS_RPC_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/value.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELOS_RPC_VERSION 1
#define TELOS_RPC_MAX_FRAME_SIZE (1024U * 1024U)

bool telos_rpc_write_frame(
    int file_descriptor,
    const struct telos_value *message,
    struct telos_error **error
);

struct telos_value *telos_rpc_read_frame(
    int file_descriptor,
    size_t maximum_size,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
