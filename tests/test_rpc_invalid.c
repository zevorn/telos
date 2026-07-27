#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <telos/rpc.h>

static bool rejects(const unsigned char *frame, size_t size, size_t maximum)
{
    int sockets[2];
    struct telos_error *error = NULL;
    struct telos_value *message;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        return false;
    }
    if (write(sockets[0], frame, size) != (ssize_t)size) {
        close(sockets[1]);
        close(sockets[0]);
        return false;
    }
    shutdown(sockets[0], SHUT_WR);
    message = telos_rpc_read_frame(sockets[1], maximum, &error);
    close(sockets[1]);
    close(sockets[0]);
    telos_value_release(message);
    if (error == NULL) {
        return false;
    }
    telos_error_release(error);
    return message == NULL;
}

int main(void)
{
    static const unsigned char truncated[] = {0, 0, 0, 8, '{', '}'};
    static const unsigned char oversized[] = {0, 16, 0, 0};
    static const unsigned char malformed[] = {
        0, 0, 0, 4, 'n', 'o', 'p', 'e',
    };
    static const unsigned char wrong_version[] = {
        0, 0, 0, 26,
        '{', '"', 'v', 'e', 'r', 's', 'i', 'o', 'n', '"', ':', '2', ',',
        '"', 't', 'y', 'p', 'e', '"', ':', '"', 'x', '"', '}',
    };

    if (
        !rejects(truncated, sizeof(truncated), TELOS_RPC_MAX_FRAME_SIZE)
        || !rejects(oversized, sizeof(oversized), 1024)
        || !rejects(malformed, sizeof(malformed), TELOS_RPC_MAX_FRAME_SIZE)
        || !rejects(
            wrong_version,
            sizeof(wrong_version),
            TELOS_RPC_MAX_FRAME_SIZE
        )
    ) {
        fputs("RPC accepted an invalid frame\n", stderr);
        return 1;
    }
    return 0;
}
