#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <telos/rpc.h>

static struct telos_value *message(const char *type,
                                   const struct telos_value *body)
{
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *type_value = telos_value_new_string(type);
    const char *keys[] = {"version", "type", "body"};
    const struct telos_value *values[] = {version, type_value, body};
    struct telos_value *result = telos_value_new_object(keys, values, 3);

    telos_value_release(type_value);
    telos_value_release(version);
    return result;
}

static bool exchange(int write_descriptor,
                     int read_descriptor,
                     const char *request_type,
                     const struct telos_value *body,
                     const char *response_type,
                     const char *response_body)
{
    struct telos_value *request = message(request_type, body);
    struct telos_value *response;
    struct telos_error *error = NULL;
    const char *actual_type;
    const char *actual_body;
    bool passed;

    if (!telos_rpc_write_frame(write_descriptor, request, &error)) {
        telos_error_release(error);
        telos_value_release(request);
        return false;
    }
    telos_value_release(request);
    response =
        telos_rpc_read_frame(read_descriptor, TELOS_RPC_MAX_FRAME_SIZE, &error);
    actual_type = telos_value_string(telos_value_get(response, "type"));
    actual_body = telos_value_string(telos_value_get(response, "body"));
    passed = response != NULL && error == NULL && actual_type != NULL &&
             strcmp(actual_type, response_type) == 0 && actual_body != NULL &&
             strcmp(actual_body, response_body) == 0;
    telos_error_release(error);
    telos_value_release(response);
    return passed;
}

int main(int argc, char **argv)
{
    int input[2];
    int output[2];
    pid_t child;
    int status;
    struct telos_value *null_value;
    struct telos_value *echo_body;
    bool passed;

    if (argc != 2 || pipe(input) != 0 || pipe(output) != 0) {
        return 1;
    }
    child = fork();
    if (child < 0) {
        return 1;
    }
    if (child == 0) {
        close(input[1]);
        close(output[0]);
        if (dup2(input[0], STDIN_FILENO) < 0 ||
            dup2(output[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(input[0]);
        close(output[1]);
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }

    close(input[0]);
    close(output[1]);
    null_value = telos_value_new_null();
    echo_body = telos_value_new_string("hello from Core");
    passed = exchange(input[1], output[0], "health", null_value,
                      "health.result", "healthy") &&
             exchange(input[1], output[0], "echo", echo_body, "echo.result",
                      "hello from Core") &&
             exchange(input[1], output[0], "shutdown", null_value,
                      "shutdown.result", "stopped");
    telos_value_release(echo_body);
    telos_value_release(null_value);
    close(input[1]);
    close(output[0]);
    if (waitpid(child, &status, 0) != child) {
        return 1;
    }
    passed = passed && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!passed) {
        fputs("Plugin Host process RPC scenario failed\n", stderr);
        return 1;
    }
    return 0;
}
