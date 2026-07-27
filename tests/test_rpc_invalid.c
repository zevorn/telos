#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static struct telos_value *message(
    const struct telos_value *version,
    const struct telos_value *type,
    const struct telos_value *body
)
{
    const char *keys[] = {"version", "type", "body"};
    const struct telos_value *values[] = {version, type, body};

    return telos_value_new_object(keys, values, 3);
}

static bool write_rejected(
    int descriptor,
    const struct telos_value *value
)
{
    struct telos_error *error = NULL;
    bool rejected = !telos_rpc_write_frame(descriptor, value, &error)
        && error != NULL;

    telos_error_release(error);
    return rejected;
}

int main(void)
{
    static const unsigned char zero[] = {0, 0, 0, 0};
    static const unsigned char partial_header[] = {0, 0};
    static const unsigned char truncated[] = {0, 0, 0, 8, '{', '}'};
    static const unsigned char oversized[] = {0, 16, 0, 0};
    static const unsigned char malformed[] = {
        0, 0, 0, 4, 'n', 'o', 'p', 'e',
    };
    static const unsigned char wrong_version[] = {
        0, 0, 0, 24,
        '{', '"', 'v', 'e', 'r', 's', 'i', 'o', 'n', '"', ':', '2', ',',
        '"', 't', 'y', 'p', 'e', '"', ':', '"', 'x', '"', '}',
    };
    static const unsigned char scalar[] = {
        0, 0, 0, 4, 'n', 'u', 'l', 'l',
    };
    static const unsigned char missing_version[] = {
        0, 0, 0, 12,
        '{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 'x', '"', '}',
    };
    static const unsigned char empty_type[] = {
        0, 0, 0, 23,
        '{', '"', 'v', 'e', 'r', 's', 'i', 'o', 'n', '"', ':', '1', ',',
        '"', 't', 'y', 'p', 'e', '"', ':', '"', '"', '}',
    };
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *wrong_version_value = telos_value_new_integer(2);
    struct telos_value *type = telos_value_new_string("test");
    struct telos_value *empty_type_value = telos_value_new_string("");
    struct telos_value *body = telos_value_new_null();
    struct telos_value *valid = message(version, type, body);
    struct telos_value *wrong = message(wrong_version_value, type, body);
    struct telos_value *empty = message(version, empty_type_value, body);
    struct telos_value *scalar_value = telos_value_new_string("scalar");
    struct telos_error *error = NULL;
    int descriptors[2];
    char *large_text = malloc(TELOS_RPC_MAX_FRAME_SIZE + 1);
    struct telos_value *large_body;
    struct telos_value *large;
    bool passed;

    if (large_text != NULL) {
        memset(large_text, 'x', TELOS_RPC_MAX_FRAME_SIZE);
        large_text[TELOS_RPC_MAX_FRAME_SIZE] = '\0';
    }
    large_body = large_text == NULL
        ? NULL
        : telos_value_new_string(large_text);
    large = large_body == NULL
        ? NULL
        : message(version, type, large_body);
    passed = version != NULL
        && type != NULL
        && body != NULL
        && valid != NULL
        && large != NULL
        && write_rejected(-1, valid)
        && write_rejected(STDOUT_FILENO, NULL)
        && write_rejected(STDOUT_FILENO, scalar_value)
        && write_rejected(STDOUT_FILENO, wrong)
        && write_rejected(STDOUT_FILENO, empty)
        && write_rejected(STDOUT_FILENO, large)
        && rejects(zero, sizeof(zero), TELOS_RPC_MAX_FRAME_SIZE)
        && rejects(
            partial_header,
            sizeof(partial_header),
            TELOS_RPC_MAX_FRAME_SIZE
        )
        && rejects(truncated, sizeof(truncated), TELOS_RPC_MAX_FRAME_SIZE)
        && rejects(oversized, sizeof(oversized), 1024)
        && rejects(malformed, sizeof(malformed), TELOS_RPC_MAX_FRAME_SIZE)
        && rejects(scalar, sizeof(scalar), TELOS_RPC_MAX_FRAME_SIZE)
        && rejects(
            missing_version,
            sizeof(missing_version),
            TELOS_RPC_MAX_FRAME_SIZE
        )
        && rejects(empty_type, sizeof(empty_type), TELOS_RPC_MAX_FRAME_SIZE)
        && rejects(
            wrong_version,
            sizeof(wrong_version),
            TELOS_RPC_MAX_FRAME_SIZE
        );

    passed = passed
        && telos_rpc_read_frame(-1, TELOS_RPC_MAX_FRAME_SIZE, &error) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed
        && telos_rpc_read_frame(STDIN_FILENO, 0, &error) == NULL
        && error != NULL;
    telos_error_release(error);
    error = NULL;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        passed = false;
    } else {
        close(descriptors[0]);
        passed = write_rejected(descriptors[1], valid) && passed;
        close(descriptors[1]);
    }

    if (pipe(descriptors) != 0) {
        passed = false;
    } else {
        close(descriptors[0]);
        passed = telos_rpc_read_frame(
            descriptors[0],
            TELOS_RPC_MAX_FRAME_SIZE,
            &error
        ) == NULL
            && error != NULL
            && passed;
        telos_error_release(error);
        error = NULL;
        close(descriptors[1]);
    }

    telos_value_release(large);
    telos_value_release(large_body);
    free(large_text);
    telos_value_release(scalar_value);
    telos_value_release(empty);
    telos_value_release(wrong);
    telos_value_release(valid);
    telos_value_release(body);
    telos_value_release(empty_type_value);
    telos_value_release(type);
    telos_value_release(wrong_version_value);
    telos_value_release(version);
    if (!passed) {
        fputs("RPC accepted an invalid frame\n", stderr);
        return 1;
    }
    return 0;
}
