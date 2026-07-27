#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <telos/rpc.h>

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool write_all(
    int descriptor,
    const void *data,
    size_t size,
    struct telos_error **error
)
{
    const unsigned char *cursor = data;

    while (size > 0) {
        ssize_t written = send(descriptor, cursor, size, MSG_NOSIGNAL);

        if (written < 0 && errno == ENOTSOCK) {
            written = write(descriptor, cursor, size);
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_IO,
                errno == 0 ? EPIPE : errno,
                "RPC frame write failed"
            );
            return false;
        }
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

static bool read_all(
    int descriptor,
    void *data,
    size_t size,
    struct telos_error **error
)
{
    unsigned char *cursor = data;

    while (size > 0) {
        ssize_t received = read(descriptor, cursor, size);

        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            set_error(
                error,
                received == 0
                    ? TELOS_ERROR_DOMAIN_PROTOCOL
                    : TELOS_ERROR_DOMAIN_IO,
                received == 0 ? EPROTO : errno,
                received == 0
                    ? "RPC frame ended before its declared length"
                    : "RPC frame read failed"
            );
            return false;
        }
        cursor += (size_t)received;
        size -= (size_t)received;
    }
    return true;
}

static bool message_valid(const struct telos_value *message)
{
    int64_t version = 0;
    const char *type;

    return message != NULL
        && telos_value_type(message) == TELOS_VALUE_OBJECT
        && telos_value_integer(
            telos_value_get(message, "version"),
            &version
        )
        && version == TELOS_RPC_VERSION
        && (type = telos_value_string(
            telos_value_get(message, "type")
        )) != NULL
        && type[0] != '\0';
}

bool telos_rpc_write_frame(
    int file_descriptor,
    const struct telos_value *message,
    struct telos_error **error
)
{
    size_t json_size;
    size_t payload_size;
    char *json;
    unsigned char length[4];
    bool result;

    if (error != NULL) {
        *error = NULL;
    }
    if (file_descriptor < 0 || !message_valid(message)) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "RPC message must contain protocol version 1 and a type"
        );
        return false;
    }
    json_size = telos_value_json_size(message);
    if (
        json_size == 0
        || json_size - 1 > TELOS_RPC_MAX_FRAME_SIZE
        || json_size - 1 > UINT32_MAX
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EMSGSIZE,
            "RPC message exceeds the frame limit"
        );
        return false;
    }
    payload_size = json_size - 1;
    json = malloc(json_size);
    if (json == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "RPC serialization buffer allocation failed"
        );
        return false;
    }
    if (!telos_value_write_json(message, json, json_size, NULL, error)) {
        free(json);
        return false;
    }

    length[0] = (unsigned char)((payload_size >> 24) & 0xff);
    length[1] = (unsigned char)((payload_size >> 16) & 0xff);
    length[2] = (unsigned char)((payload_size >> 8) & 0xff);
    length[3] = (unsigned char)(payload_size & 0xff);
    result = write_all(
        file_descriptor,
        length,
        sizeof(length),
        error
    ) && write_all(file_descriptor, json, payload_size, error);
    free(json);
    return result;
}

struct telos_value *telos_rpc_read_frame(
    int file_descriptor,
    size_t maximum_size,
    struct telos_error **error
)
{
    unsigned char length[4];
    uint32_t payload_size;
    char *json;
    struct telos_value *message;

    if (error != NULL) {
        *error = NULL;
    }
    if (file_descriptor < 0 || maximum_size == 0) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "RPC descriptor and maximum frame size are required"
        );
        return NULL;
    }
    if (!read_all(file_descriptor, length, sizeof(length), error)) {
        return NULL;
    }
    payload_size = ((uint32_t)length[0] << 24)
        | ((uint32_t)length[1] << 16)
        | ((uint32_t)length[2] << 8)
        | (uint32_t)length[3];
    if (payload_size == 0 || payload_size > maximum_size) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EMSGSIZE,
            "RPC frame length is invalid"
        );
        return NULL;
    }

    json = malloc((size_t)payload_size + 1);
    if (json == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "RPC frame allocation failed"
        );
        return NULL;
    }
    if (!read_all(file_descriptor, json, payload_size, error)) {
        free(json);
        return NULL;
    }
    json[payload_size] = '\0';
    message = telos_value_parse_json(json, payload_size, error);
    free(json);
    if (message == NULL) {
        return NULL;
    }
    if (!message_valid(message)) {
        telos_value_release(message);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTONOSUPPORT,
            "RPC protocol version or message type is invalid"
        );
        return NULL;
    }
    return message;
}
