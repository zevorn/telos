#include <curl/curl.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <telos/plugins/curl_transport.h>

struct receive_context {
    telos_transport_chunk_fn receive;
    void *context;
    struct telos_error **error;
    const struct telos_cancel *cancel;
    bool callback_failed;
};

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static CURLcode curl_initialization = CURLE_FAILED_INIT;

static void initialize_curl(void)
{
    curl_initialization = curl_global_init(CURL_GLOBAL_DEFAULT);
}

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool host_matches(const char *host, const char *expected)
{
    size_t size = strlen(expected);

    return strncmp(host, expected, size) == 0 &&
           (host[size] == '\0' || host[size] == '/' || host[size] == ':');
}

static bool url_allowed(const char *url)
{
    const char *host;

    if (strncmp(url, "https://", 8) == 0) {
        return true;
    }
    if (strncmp(url, "http://", 7) != 0) {
        return false;
    }
    host = url + 7;
    return host_matches(host, "localhost") ||
           host_matches(host, "127.0.0.1") || host_matches(host, "[::1]");
}

static size_t receive_data(char *data, size_t size, size_t count,
                           void *context)
{
    struct receive_context *receiver = context;
    size_t total;

    if (size != 0 && count > SIZE_MAX / size) {
        receiver->callback_failed = true;
        set_error(receiver->error, TELOS_ERROR_DOMAIN_MEMORY, EOVERFLOW,
                  "curl response chunk is too large");
        return 0;
    }
    total = size * count;
    if (total > 0 &&
        !receiver->receive(data, total, receiver->context, receiver->error)) {
        receiver->callback_failed = true;
        return 0;
    }
    return total;
}

static int transfer_progress(void *context, curl_off_t download_total,
                             curl_off_t download_current,
                             curl_off_t upload_total,
                             curl_off_t upload_current)
{
    const struct receive_context *receiver = context;

    (void)download_total;
    (void)download_current;
    (void)upload_total;
    (void)upload_current;
    return telos_cancel_requested(receiver->cancel) ? 1 : 0;
}

static CURLcode configure_curl(CURL *curl,
                               const telos_transport_request *request,
                               struct curl_slist *headers,
                               struct receive_context *receiver,
                               long timeout_milliseconds,
                               char error_buffer[CURL_ERROR_SIZE])
{
    CURLcode result;

    result = curl_easy_setopt(curl, CURLOPT_URL, request->url);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_POST, 1L);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                              (curl_off_t)request->body_size);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (result != CURLE_OK) {
        return result;
    }
    if (request->bearer_token != NULL && request->bearer_token[0] != '\0') {
        result = curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
        if (result != CURLE_OK) {
            return result;
        }
        result = curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER,
                                  request->bearer_token);
        if (result != CURLE_OK) {
            return result;
        }
    }
    result = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_data);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_WRITEDATA, receiver);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                              transfer_progress);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_XFERINFODATA, receiver);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_NOPROXY,
                              "localhost,127.0.0.1,[::1]");
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                              timeout_milliseconds);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (result != CURLE_OK) {
        return result;
    }
    result = curl_easy_setopt(curl, CURLOPT_USERAGENT,
                              "telos-curl-transport/0.1");
    if (result != CURLE_OK) {
        return result;
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    result = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    result = curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                              CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    return result;
}

bool telos_curl_transport_send(const telos_transport_request *request,
                               telos_transport_chunk_fn receive,
                               void *receive_context,
                               int *status_code,
                               void *transport_context,
                               struct telos_error **error)
{
    const struct telos_curl_transport_config *config = transport_context;
    long timeout_milliseconds = TELOS_CURL_DEFAULT_TIMEOUT_MILLISECONDS;
    struct receive_context receiver = {
        .receive = receive,
        .context = receive_context,
        .error = error,
    };
    char content_type[256];
    char error_buffer[CURL_ERROR_SIZE] = {0};
    struct curl_slist *headers = NULL;
    struct curl_slist *next_header;
    CURL *curl = NULL;
    CURLcode result;
    long response_code;
    bool success = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (config != NULL && config->timeout_milliseconds != 0) {
        timeout_milliseconds = config->timeout_milliseconds;
    }
    if (request == NULL || receive == NULL || status_code == NULL ||
        request->method == NULL || strcmp(request->method, "POST") != 0 ||
        request->url == NULL || !url_allowed(request->url) ||
        request->content_type == NULL ||
        strpbrk(request->content_type, "\r\n") != NULL ||
        (request->bearer_token != NULL &&
         strpbrk(request->bearer_token, "\r\n") != NULL) ||
        request->body == NULL ||
        request->body_size > (size_t)INT64_MAX ||
        timeout_milliseconds <= 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "curl Transport request is invalid");
        return false;
    }
    if (telos_cancel_requested(request->cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "curl Transport request was cancelled");
        return false;
    }
    receiver.cancel = request->cancel;
    if (pthread_once(&curl_once, initialize_curl) != 0 ||
        curl_initialization != CURLE_OK) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "libcurl initialization failed");
        return false;
    }
    curl = curl_easy_init();
    if (curl == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "curl handle allocation failed");
        return false;
    }
    if (snprintf(content_type, sizeof(content_type), "Content-Type: %s",
                 request->content_type) >= (int)sizeof(content_type)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "curl Content-Type is too long");
        goto cleanup;
    }
    next_header = curl_slist_append(headers, content_type);
    if (next_header == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "curl header allocation failed");
        goto cleanup;
    }
    headers = next_header;
    next_header = curl_slist_append(headers, "Accept: text/event-stream");
    if (next_header == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "curl header allocation failed");
        goto cleanup;
    }
    headers = next_header;
    result = configure_curl(curl, request, headers, &receiver,
                            timeout_milliseconds, error_buffer);
    if (result == CURLE_OK) {
        result = curl_easy_perform(curl);
    }
    if (result != CURLE_OK || receiver.callback_failed) {
        if (telos_cancel_requested(request->cancel)) {
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "curl Transport request was cancelled");
        } else if (receiver.callback_failed) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "curl response receiver rejected a chunk");
        } else {
            set_error(error, TELOS_ERROR_DOMAIN_IO,
                      result == CURLE_OPERATION_TIMEDOUT ? ETIMEDOUT : EIO,
                      error_buffer[0] == '\0'
                          ? curl_easy_strerror(result)
                          : error_buffer);
        }
        goto cleanup;
    }
    result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (result != CURLE_OK || response_code < 0 ||
        response_code > INT_MAX) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "curl response status is invalid");
        goto cleanup;
    }
    *status_code = (int)response_code;
    success = true;

cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return success;
}
