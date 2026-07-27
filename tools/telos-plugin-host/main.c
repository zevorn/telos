#include <stdio.h>
#include <string.h>

#include <telos/rpc.h>

static struct telos_value *response(
    const char *type,
    const struct telos_value *body
)
{
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *type_value = telos_value_new_string(type);
    const char *keys[] = {"version", "type", "body"};
    const struct telos_value *values[] = {version, type_value, body};
    struct telos_value *message = telos_value_new_object(keys, values, 3);

    telos_value_release(type_value);
    telos_value_release(version);
    return message;
}

int main(void)
{
    for (;;) {
        struct telos_error *error = NULL;
        struct telos_value *request = telos_rpc_read_frame(
            0,
            TELOS_RPC_MAX_FRAME_SIZE,
            &error
        );
        const char *type;
        struct telos_value *body;
        struct telos_value *message;
        bool stopping = false;

        if (request == NULL) {
            fprintf(
                stderr,
                "telos-plugin-host: %s\n",
                telos_error_message(error)
            );
            telos_error_release(error);
            return 1;
        }
        type = telos_value_string(telos_value_get(request, "type"));
        if (strcmp(type, "health") == 0) {
            body = telos_value_new_string("healthy");
            message = response("health.result", body);
        } else if (strcmp(type, "echo") == 0) {
            const struct telos_value *request_body =
                telos_value_get(request, "body");

            body = request_body == NULL
                ? telos_value_new_null()
                : telos_value_retain(request_body);
            message = response("echo.result", body);
        } else if (strcmp(type, "shutdown") == 0) {
            body = telos_value_new_string("stopped");
            message = response("shutdown.result", body);
            stopping = true;
        } else {
            body = telos_value_new_string("unknown message type");
            message = response("error", body);
        }
        telos_value_release(body);
        telos_value_release(request);

        if (
            message == NULL
            || !telos_rpc_write_frame(1, message, &error)
        ) {
            fprintf(
                stderr,
                "telos-plugin-host: %s\n",
                telos_error_message(error)
            );
            telos_error_release(error);
            telos_value_release(message);
            return 1;
        }
        telos_value_release(message);
        if (stopping) {
            return 0;
        }
    }
}
