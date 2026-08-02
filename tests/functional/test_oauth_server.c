#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define REQUEST_SIZE (64U * 1024U)

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

static bool send_response(int descriptor, const char *content_type,
                          const char *body)
{
    char header[512];
    int size = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n\r\n",
                        content_type, strlen(body));

    return size > 0 && (size_t)size < sizeof(header) &&
           send_all(descriptor, header, (size_t)size) &&
           send_all(descriptor, body, strlen(body));
}

static int receive_request(int listener, const char *marker,
                           char request[REQUEST_SIZE])
{
    struct timeval timeout = {
        .tv_sec = 5,
    };
    size_t used = 0;
    int client = accept(listener, NULL, NULL);

    if (client < 0 ||
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) != 0) {
        if (client >= 0) {
            close(client);
        }
        return -1;
    }
    while (used + 1 < REQUEST_SIZE) {
        ssize_t received = recv(client, request + used,
                                REQUEST_SIZE - used - 1, 0);

        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        used += (size_t)received;
        request[used] = '\0';
        if (strstr(request, marker) != NULL) {
            return client;
        }
    }
    close(client);
    return -1;
}

static bool handle_request(int listener, const char *marker,
                           const char *path, const char *required,
                           const char *content_type, const char *response)
{
    char request[REQUEST_SIZE] = {0};
    int client = receive_request(listener, marker, request);
    bool result;

    if (client < 0 || strstr(request, path) == NULL ||
        (required != NULL && strstr(request, required) == NULL) ||
        (strstr(path, "/v1/responses") != NULL &&
         (strstr(request, "Authorization: Bearer e30.") == NULL ||
          strstr(request, "originator: telos") == NULL ||
          strstr(request,
                 "OpenAI-Beta: responses=experimental") == NULL))) {
        if (client >= 0) {
            close(client);
        }
        return false;
    }
    result = send_response(client, content_type, response);
    close(client);
    return result;
}

int main(void)
{
    static const char user_code[] =
        "{\"device_auth_id\":\"device-functional\","
        "\"user_code\":\"TEST-CODE\",\"interval\":0}";
    static const char device_token[] =
        "{\"authorization_code\":\"authorization-functional\","
        "\"code_verifier\":\"verifier-functional\"}";
    static const char token[] =
        "{\"access_token\":\"e30."
        "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
        "X2lkIjoiYWNjdC1mdW5jdGlvbmFsIn19.signature\","
        "\"refresh_token\":\"refresh-functional\","
        "\"expires_in\":3600}";
    static const char response[] =
        "event: response.created\n"
        "data: {\"type\":\"response.created\",\"response\":"
        "{\"id\":\"resp_oauth\"}}\n\n"
        "event: response.output_text.delta\n"
        "data: {\"type\":\"response.output_text.delta\","
        "\"item_id\":\"message_1\",\"delta\":\"OAuth works\"}\n\n"
        "event: response.completed\n"
        "data: {\"type\":\"response.completed\",\"response\":"
        "{\"id\":\"resp_oauth\"}}\n\n"
        "data: [DONE]\n\n";
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    socklen_t address_size = sizeof(address);
    int listener = -1;
    int enabled = 1;
    int result = 1;

    signal(SIGPIPE, SIG_IGN);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled,
                   sizeof(enabled)) != 0 ||
        bind(listener, (const struct sockaddr *)&address, sizeof(address)) !=
            0 ||
        listen(listener, 4) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &address_size) !=
            0) {
        goto cleanup;
    }
    printf("%u\n", (unsigned int)ntohs(address.sin_port));
    fflush(stdout);
    if (!handle_request(listener, "app_EMoamEEZ73f0CkXaXp7hrann",
                        "POST /api/accounts/deviceauth/usercode HTTP/",
                        "\"client_id\":\"app_EMoamEEZ73f0CkXaXp7hrann\"",
                        "application/json", user_code) ||
        !handle_request(listener, "device-functional",
                        "POST /api/accounts/deviceauth/token HTTP/",
                        "\"user_code\":\"TEST-CODE\"",
                        "application/json", device_token) ||
        !handle_request(listener, "authorization-functional",
                        "POST /oauth/token HTTP/",
                        "code_verifier=verifier-functional",
                        "application/json", token) ||
        !handle_request(listener, "\"stream\":true",
                        "POST /v1/responses HTTP/",
                        "chatgpt-account-id: acct-functional",
                        "text/event-stream", response)) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (listener >= 0) {
        close(listener);
    }
    return result;
}
