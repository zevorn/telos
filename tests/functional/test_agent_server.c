#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

int main(void)
{
    static const char responses_response[] =
        "event: response.created\n"
        "data: {\"type\":\"response.created\",\"response\":"
        "{\"id\":\"resp_functional\"}}\n\n"
        "event: response.output_text.delta\n"
        "data: {\"type\":\"response.output_text.delta\","
        "\"item_id\":\"message_1\",\"delta\":"
        "\"hello from functional test\"}\n\n"
        "event: response.completed\n"
        "data: {\"type\":\"response.completed\",\"response\":"
        "{\"id\":\"resp_functional\"}}\n\n"
        "data: [DONE]\n\n";
    static const char chat_response[] =
        "data: {\"id\":\"chatcmpl_functional\",\"choices\":[{"
        "\"index\":0,\"delta\":{\"content\":"
        "\"hello from chat functional test\"},\"finish_reason\":"
        "\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    socklen_t address_size = sizeof(address);
    struct timeval timeout = {
        .tv_sec = 5,
    };
    char request[64U * 1024U];
    char header[256];
    size_t used = 0;
    int listener = -1;
    int client = -1;
    int result = 1;
    int enabled = 1;
    int header_size;
    const char *response;
    bool chat_request;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled,
                   sizeof(enabled)) != 0 ||
        bind(listener, (const struct sockaddr *)&address, sizeof(address)) !=
            0 ||
        listen(listener, 1) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &address_size) !=
            0) {
        goto cleanup;
    }
    printf("%u\n", (unsigned int)ntohs(address.sin_port));
    fflush(stdout);
    client = accept(listener, NULL, NULL);
    if (client < 0 ||
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) != 0) {
        goto cleanup;
    }
    while (used + 1 < sizeof(request)) {
        ssize_t received = recv(client, request + used,
                                sizeof(request) - used - 1, 0);

        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        used += (size_t)received;
        request[used] = '\0';
        if (strstr(request, "\"stream\":true") != NULL) {
            break;
        }
    }
    chat_request = strstr(request, "POST /v1/chat/completions HTTP/") != NULL;
    if (!chat_request && strstr(request, "POST /v1/responses HTTP/") == NULL) {
        goto cleanup;
    }
    if ((!chat_request &&
         strstr(request, "\"model\":\"local-model\"") == NULL) ||
        (chat_request &&
         strstr(request, "\"model\":\"deepseek-chat\"") == NULL) ||
        strstr(request, "\"content\":\"hello\"") == NULL ||
        strstr(request, "# KERNEL CONTRACT") == NULL) {
        goto cleanup;
    }
    if (chat_request && strstr(request, "\"messages\"") == NULL) {
        goto cleanup;
    }
    response = chat_request ? chat_response : responses_response;
    header_size = snprintf(header, sizeof(header),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/event-stream\r\n"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n\r\n",
                           strlen(response));
    if (header_size <= 0 || (size_t)header_size >= sizeof(header) ||
        !send_all(client, header, (size_t)header_size) ||
        !send_all(client, response, strlen(response))) {
        goto cleanup;
    }
    result = 0;

cleanup:
    if (client >= 0) {
        close(client);
    }
    if (listener >= 0) {
        close(listener);
    }
    return result;
}
