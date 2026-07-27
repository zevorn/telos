#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <telos/plugin_process.h>
#include <telos/rpc.h>

struct telos_plugin_process {
    pthread_mutex_t mutex;
    pid_t child;
    int input;
    int output;
    bool stopped;
};

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

static int64_t monotonic_milliseconds(void)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        return 0;
    }
    return (int64_t)current.tv_sec * INT64_C(1000)
        + current.tv_nsec / INT64_C(1000000);
}

static struct telos_value *request_message(
    const char *type,
    const struct telos_value *body
)
{
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *type_value = telos_value_new_string(type);
    const char *keys[] = {"version", "type", "body"};
    const struct telos_value *values[] = {version, type_value, body};
    struct telos_value *message = NULL;

    if (version != NULL && type_value != NULL && body != NULL) {
        message = telos_value_new_object(keys, values, 3);
    }
    telos_value_release(type_value);
    telos_value_release(version);
    return message;
}

struct telos_plugin_process *telos_plugin_process_spawn(
    const char *host_path,
    struct telos_error **error
)
{
    int sockets[2];
    pid_t child;
    struct telos_plugin_process *process;

    if (error != NULL) {
        *error = NULL;
    }
    if (host_path == NULL || host_path[0] == '\0') {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin Host path is required"
        );
        return NULL;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Plugin Host socket could not be created"
        );
        return NULL;
    }
    child = fork();
    if (child < 0) {
        close(sockets[1]);
        close(sockets[0]);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_IO,
            errno,
            "Plugin Host process could not be created"
        );
        return NULL;
    }
    if (child == 0) {
        setpgid(0, 0);
        close(sockets[0]);
        if (
            dup2(sockets[1], STDIN_FILENO) < 0
            || dup2(sockets[1], STDOUT_FILENO) < 0
        ) {
            _exit(126);
        }
        close(sockets[1]);
        execl(host_path, host_path, (char *)NULL);
        _exit(127);
    }
    close(sockets[1]);
    process = calloc(1, sizeof(*process));
    if (process == NULL) {
        close(sockets[0]);
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin Host process allocation failed"
        );
        return NULL;
    }
    if (pthread_mutex_init(&process->mutex, NULL) != 0) {
        close(sockets[0]);
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        free(process);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EAGAIN,
            "Plugin Host mutex could not be initialized"
        );
        return NULL;
    }
    process->child = child;
    process->input = sockets[0];
    process->output = sockets[0];
    return process;
}

static bool response_ready(
    struct telos_plugin_process *process,
    unsigned int timeout_milliseconds,
    const struct telos_cancel *cancel,
    struct telos_error **error
)
{
    int64_t started = monotonic_milliseconds();

    for (;;) {
        struct pollfd descriptor = {
            .fd = process->output,
            .events = POLLIN | POLLHUP,
        };
        int poll_result = poll(&descriptor, 1, 10);

        if (poll_result > 0) {
            return true;
        }
        if (poll_result < 0 && errno != EINTR) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_IO,
                errno,
                "Plugin Host response wait failed"
            );
            return false;
        }
        if (telos_cancel_requested(cancel)) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_CANCELLED,
                ECANCELED,
                "Plugin request was cancelled"
            );
            return false;
        }
        if (
            timeout_milliseconds > 0
            && monotonic_milliseconds() - started
                >= (int64_t)timeout_milliseconds
        ) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_TIMEOUT,
                ETIMEDOUT,
                "Plugin Host request timed out"
            );
            return false;
        }
    }
}

static void wrap_process_error(struct telos_error **error)
{
    struct telos_error *cause;

    if (error == NULL) {
        return;
    }
    cause = *error;
    *error = telos_error_create(
        TELOS_ERROR_DOMAIN_PLUGIN,
        EPIPE,
        "Plugin Host crashed or disconnected",
        cause
    );
    telos_error_release(cause);
}

bool telos_plugin_process_request(
    struct telos_plugin_process *process,
    const char *type,
    const struct telos_value *body,
    unsigned int timeout_milliseconds,
    const struct telos_cancel *cancel,
    struct telos_value **response_body,
    struct telos_error **error
)
{
    struct telos_value *request;
    struct telos_value *response = NULL;
    const char *response_type;
    const struct telos_value *body_value;
    char expected[256];
    bool result = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (response_body != NULL) {
        *response_body = NULL;
    }
    if (
        process == NULL
        || type == NULL
        || type[0] == '\0'
        || strlen(type) > 240
        || body == NULL
        || response_body == NULL
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin Host request arguments are invalid"
        );
        return false;
    }
    request = request_message(type, body);
    if (request == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin Host request allocation failed"
        );
        return false;
    }
    pthread_mutex_lock(&process->mutex);
    if (process->stopped) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PLUGIN,
            EPIPE,
            "Plugin Host is not running"
        );
        goto cleanup;
    }
    if (!telos_rpc_write_frame(process->input, request, error)) {
        wrap_process_error(error);
        process->stopped = true;
        goto cleanup;
    }
    if (!response_ready(
        process,
        timeout_milliseconds,
        cancel,
        error
    )) {
        goto cleanup;
    }
    response = telos_rpc_read_frame(
        process->output,
        TELOS_RPC_MAX_FRAME_SIZE,
        error
    );
    if (response == NULL) {
        wrap_process_error(error);
        process->stopped = true;
        goto cleanup;
    }
    response_type = telos_value_string(telos_value_get(response, "type"));
    body_value = telos_value_get(response, "body");
    snprintf(expected, sizeof(expected), "%s.result", type);
    if (
        response_type == NULL
        || strcmp(response_type, expected) != 0
        || body_value == NULL
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "Plugin Host returned an unexpected response"
        );
        goto cleanup;
    }
    *response_body = telos_value_retain(body_value);
    result = *response_body != NULL;

cleanup:
    telos_value_release(response);
    pthread_mutex_unlock(&process->mutex);
    telos_value_release(request);
    return result;
}

bool telos_plugin_process_shutdown(
    struct telos_plugin_process *process,
    unsigned int timeout_milliseconds,
    struct telos_error **error
)
{
    struct telos_value *body;
    struct telos_value *response = NULL;
    bool result;

    if (error != NULL) {
        *error = NULL;
    }
    if (process == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin Host process is required"
        );
        return false;
    }
    body = telos_value_new_null();
    if (body == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin shutdown allocation failed"
        );
        return false;
    }
    result = telos_plugin_process_request(
        process,
        "shutdown",
        body,
        timeout_milliseconds,
        NULL,
        &response,
        error
    );
    telos_value_release(response);
    telos_value_release(body);
    if (result) {
        pthread_mutex_lock(&process->mutex);
        process->stopped = true;
        pthread_mutex_unlock(&process->mutex);
    }
    return result;
}

void telos_plugin_process_destroy(struct telos_plugin_process *process)
{
    int status;

    if (process == NULL) {
        return;
    }
    close(process->input);
    if (waitpid(process->child, &status, WNOHANG) == 0) {
        kill(-process->child, SIGKILL);
        waitpid(process->child, &status, 0);
    }
    pthread_mutex_destroy(&process->mutex);
    free(process);
}
