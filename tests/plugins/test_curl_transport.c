#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <telos/plugins/curl_transport.h>

static bool receive_chunk(const char *data, size_t size, void *context,
                          struct telos_error **error)
{
    (void)data;
    (void)size;
    (void)context;
    (void)error;
    return true;
}

static bool reject_chunk(const char *data, size_t size, void *context,
                         struct telos_error **error)
{
    (void)data;
    (void)size;
    (void)context;
    (void)error;
    return false;
}

static bool reject_with_error(const char *data, size_t size, void *context,
                              struct telos_error **error)
{
    (void)data;
    (void)size;
    (void)context;
    *error = telos_error_create(TELOS_ERROR_DOMAIN_STATE, EIO,
                                "receiver rejected response", NULL);
    return false;
}

static bool cancel_after_chunk(const char *data, size_t size, void *context,
                               struct telos_error **error)
{
    struct telos_cancel *cancel = context;

    (void)data;
    (void)size;
    (void)error;
    telos_cancel_request(cancel);
    return true;
}

enum server_mode {
    SERVER_RESPOND = 1,
    SERVER_STALL,
    SERVER_SPLIT,
};

static bool send_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t sent = send(descriptor, data, size, 0);

        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data += (size_t)sent;
        size -= (size_t)sent;
    }
    return true;
}

static pid_t start_server(enum server_mode mode, char *url, size_t url_size)
{
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    socklen_t address_size = sizeof(address);
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    pid_t child;

    assert(listener >= 0);
    assert(bind(listener, (const struct sockaddr *)&address,
                sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    assert(getsockname(listener, (struct sockaddr *)&address,
                       &address_size) == 0);
    assert(snprintf(url, url_size, "http://127.0.0.1:%u/v1/responses",
                    (unsigned int)ntohs(address.sin_port)) > 0);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        static const char response[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 7\r\n"
            "Connection: close\r\n\r\n"
            "payload";
        static const char split_header[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 16\r\n"
            "Connection: close\r\n\r\n";
        const struct timespec delay = {
            .tv_nsec = mode == SERVER_STALL ? 100000000 : 200000000,
        };
        char request_data[1024];
        int client;

        signal(SIGPIPE, SIG_IGN);
        client = accept(listener, NULL, NULL);
        if (client < 0) {
            _exit(2);
        }
        recv(client, request_data, sizeof(request_data), 0);
        if (mode == SERVER_RESPOND) {
            send_all(client, response, sizeof(response) - 1);
        } else if (mode == SERVER_STALL) {
            nanosleep(&delay, NULL);
        } else {
            send_all(client, split_header, sizeof(split_header) - 1);
            send_all(client, "x", 1);
            nanosleep(&delay, NULL);
            send_all(client, "xxxxxxxxxxxxxxx", 15);
        }
        close(client);
        close(listener);
        _exit(0);
    }
    close(listener);
    return child;
}

static void wait_server(pid_t child)
{
    int status;

    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

static void expect_error(const struct telos_transport_request *request,
                         telos_transport_chunk_fn receive,
                         int *status,
                         const struct telos_curl_transport_config *config,
                         enum telos_error_domain domain)
{
    struct telos_error *error = NULL;

    assert(!telos_curl_transport_send(request, receive, NULL, status,
                                      (void *)config, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == domain);
    telos_error_release(error);
}

int main(void)
{
    static const char body[] = "{}";
    struct telos_cancel *cancel = telos_cancel_create();
    struct telos_transport_request request = {
        .method = "GET",
        .url = "http://127.0.0.1:1/v1/responses",
        .content_type = "application/json",
        .body = body,
        .body_size = sizeof(body) - 1,
        .cancel = cancel,
    };
    struct telos_curl_transport_config config = {
        .timeout_milliseconds = 0,
    };
    char long_content_type[300];
    char url[128];
    int status = 0;

    assert(cancel != NULL);
    assert(!telos_curl_transport_send(NULL, receive_chunk, NULL, &status,
                                      NULL, NULL));
    expect_error(NULL, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    expect_error(&request, NULL, &status, NULL, TELOS_ERROR_DOMAIN_ARGUMENT);
    expect_error(&request, receive_chunk, NULL, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);

    request.method = NULL;
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.method = "GET";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);

    request.method = "POST";
    request.url = NULL;
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "ftp://127.0.0.1/file";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "http://example.com/v1/responses";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "http://127.0.0.1:1/v1/responses";

    request.content_type = NULL;
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.content_type = "application/json\r\nInjected: value";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);

    request.content_type = "application/json";
    request.bearer_token = "bad\ntoken";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.bearer_token = NULL;
    request.body = NULL;
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.body = body;
#if SIZE_MAX > INT64_MAX
    request.body_size = (size_t)INT64_MAX + 1U;
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.body_size = sizeof(body) - 1;
#endif
    config.timeout_milliseconds = -1;
    expect_error(&request, receive_chunk, &status, &config,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    config.timeout_milliseconds = 0;

    memset(long_content_type, 'a', sizeof(long_content_type) - 1);
    long_content_type[sizeof(long_content_type) - 1] = '\0';
    request.content_type = long_content_type;
    expect_error(&request, receive_chunk, &status, &config,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.content_type = "application/json";

    request.cancel = NULL;
    request.bearer_token = "";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_IO);
    request.url = "http://localhost:1/v1/responses";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_IO);
    request.url = "http://[::1]:1/v1/responses";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_IO);
    request.url = "http://localhost.example/v1/responses";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "http://localhost@invalid/v1/responses";
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_ARGUMENT);
    request.url = "https://127.0.0.1:1/v1/responses";
    expect_error(&request, receive_chunk, &status, &config,
                 TELOS_ERROR_DOMAIN_IO);

    request.url = "http://127.0.0.1:1/v1/responses";
    request.cancel = cancel;
    request.bearer_token = NULL;
    assert(telos_cancel_request(cancel));
    expect_error(&request, receive_chunk, &status, NULL,
                 TELOS_ERROR_DOMAIN_CANCELLED);
    telos_cancel_release(cancel);

    cancel = telos_cancel_create();
    assert(cancel != NULL);
    request.cancel = cancel;
    request.url = url;
    {
        pid_t server = start_server(SERVER_RESPOND, url, sizeof(url));

        expect_error(&request, reject_chunk, &status, NULL,
                     TELOS_ERROR_DOMAIN_IO);
        wait_server(server);
    }
    {
        pid_t server = start_server(SERVER_RESPOND, url, sizeof(url));

        expect_error(&request, reject_with_error, &status, NULL,
                     TELOS_ERROR_DOMAIN_STATE);
        wait_server(server);
    }
    {
        pid_t server = start_server(SERVER_STALL, url, sizeof(url));

        config.timeout_milliseconds = 20;
        expect_error(&request, receive_chunk, &status, &config,
                     TELOS_ERROR_DOMAIN_IO);
        wait_server(server);
    }
    {
        struct telos_error *error = NULL;
        pid_t server = start_server(SERVER_SPLIT, url, sizeof(url));

        config.timeout_milliseconds = 1000;
        assert(!telos_curl_transport_send(&request, cancel_after_chunk, cancel,
                                          &status, &config, &error));
        assert(error != NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_CANCELLED);
        telos_error_release(error);
        wait_server(server);
    }
    telos_cancel_release(cancel);
    return 0;
}
