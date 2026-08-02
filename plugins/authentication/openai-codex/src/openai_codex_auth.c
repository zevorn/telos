#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <telos/plugins/openai_codex_auth.h>
#include <telos/value.h>

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define OPENAI_CODEX_CLIENT_ID "app_EMoamEEZ73f0CkXaXp7hrann"
#define OPENAI_CODEX_PROVIDER "openai-codex"
#define OPENAI_CODEX_SECRET_TARGET "provider.openai"
#define OPENAI_CODEX_CACHE_FILE "openai-codex-auth.json"
#define OPENAI_CODEX_PATH_SIZE 4096U
#define OPENAI_CODEX_ENDPOINT_SIZE 512U
#define OPENAI_CODEX_RESPONSE_SIZE (64U * 1024U)
#define OPENAI_CODEX_ACCESS_SIZE (16U * 1024U)
#define OPENAI_CODEX_REFRESH_SIZE (8U * 1024U)
#define OPENAI_CODEX_ACCOUNT_SIZE 256U
#define OPENAI_CODEX_DEVICE_ID_SIZE 512U
#define OPENAI_CODEX_USER_CODE_SIZE 128U
#define OPENAI_CODEX_AUTHORIZATION_CODE_SIZE 4096U
#define OPENAI_CODEX_VERIFIER_SIZE 4096U
#define OPENAI_CODEX_FORM_SIZE (16U * 1024U)
#define OPENAI_CODEX_LOGIN_TIMEOUT_SECONDS (15U * 60U)
#define OPENAI_CODEX_REFRESH_SKEW_SECONDS 120

struct telos_authentication {
    char state_directory[OPENAI_CODEX_PATH_SIZE];
    char service_endpoint[OPENAI_CODEX_ENDPOINT_SIZE];
    telos_transport_send_fn send;
    void *transport_context;
    enum telos_authentication_state state;
    char access_token[OPENAI_CODEX_ACCESS_SIZE];
    char refresh_token[OPENAI_CODEX_REFRESH_SIZE];
    char account_id[OPENAI_CODEX_ACCOUNT_SIZE];
    int64_t expires_at;
};

struct oauth_credentials {
    char access_token[OPENAI_CODEX_ACCESS_SIZE];
    char refresh_token[OPENAI_CODEX_REFRESH_SIZE];
    char account_id[OPENAI_CODEX_ACCOUNT_SIZE];
    int64_t expires_at;
};

struct response_buffer {
    char data[OPENAI_CODEX_RESPONSE_SIZE];
    size_t size;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static void secure_wipe(void *data, size_t size)
{
    /* Volatile keeps secret erasure observable to the compiler. */
    volatile unsigned char *cursor = data;

    while (size > 0) {
        *cursor++ = 0;
        --size;
    }
}

static bool copy_text(char *target, size_t capacity, const char *source)
{
    size_t size;

    if (target == NULL || capacity == 0 || source == NULL) {
        return false;
    }
    size = strlen(source);
    if (size >= capacity) {
        return false;
    }
    memcpy(target, source, size + 1);
    return true;
}

static char *duplicate_secret(const char *source)
{
    size_t size = strlen(source) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, source, size);
    }
    return copy;
}

static bool host_matches(const char *host, const char *expected)
{
    size_t size = strlen(expected);

    return strncmp(host, expected, size) == 0 &&
           (host[size] == '\0' || host[size] == '/' || host[size] == ':');
}

static bool endpoint_is_official(const char *endpoint)
{
    size_t expected_size = strlen(TELOS_OPENAI_CODEX_AUTH_ENDPOINT);

    if (endpoint == NULL ||
        strncmp(endpoint, TELOS_OPENAI_CODEX_AUTH_ENDPOINT,
                expected_size) != 0) {
        return false;
    }
    endpoint += expected_size;
    while (*endpoint == '/') {
        ++endpoint;
    }
    return *endpoint == '\0';
}

static bool endpoint_is_loopback(const char *endpoint)
{
    const char *host;

    if (endpoint == NULL) {
        return false;
    }
    if (strncmp(endpoint, "http://", 7) == 0) {
        host = endpoint + 7;
    } else if (strncmp(endpoint, "https://", 8) == 0) {
        host = endpoint + 8;
    } else {
        return false;
    }
    return host_matches(host, "localhost") ||
           host_matches(host, "127.0.0.1") || host_matches(host, "[::1]");
}

static bool endpoint_allowed(const char *endpoint)
{
    return endpoint_is_official(endpoint) || endpoint_is_loopback(endpoint);
}

static bool make_path(const struct telos_authentication *authentication,
                      const char *name, char path[OPENAI_CODEX_PATH_SIZE],
                      struct telos_error **error)
{
    if (snprintf(path, OPENAI_CODEX_PATH_SIZE, "%s/%s",
                 authentication->state_directory, name) >=
        (int)OPENAI_CODEX_PATH_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "OpenAI authentication state path is too long");
        return false;
    }
    return true;
}

static bool
ensure_state_directory(const struct telos_authentication *authentication,
                       struct telos_error **error)
{
    struct stat status;

    if (stat(authentication->state_directory, &status) != 0) {
        if (errno != ENOENT ||
            mkdir(authentication->state_directory, 0700) != 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "OpenAI authentication state directory is unavailable");
            return false;
        }
        return true;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != getuid() ||
        (status.st_mode & 0077) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "OpenAI authentication state directory is not private");
        return false;
    }
    return true;
}

static bool write_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(descriptor, data, size);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        data += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

static struct telos_value *
credentials_value(const struct oauth_credentials *credentials,
                  struct telos_error **error)
{
    const char *keys[] = {
        "access_token",
        "refresh_token",
        "account_id",
        "expires_at",
    };
    struct telos_value *values[] = {
        telos_value_new_string(credentials->access_token),
        telos_value_new_string(credentials->refresh_token),
        telos_value_new_string(credentials->account_id),
        telos_value_new_integer(credentials->expires_at),
    };
    struct telos_value *result = NULL;

    if (values[0] != NULL && values[1] != NULL && values[2] != NULL &&
        values[3] != NULL) {
        result = telos_value_new_object(
            keys, (const struct telos_value *const *)values, 4);
    }
    for (size_t index = 0; index < 4; ++index) {
        telos_value_release(values[index]);
    }
    if (result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "OpenAI authentication cache allocation failed");
    }
    return result;
}

static bool
persist_credentials(const struct telos_authentication *authentication,
                    const struct oauth_credentials *credentials,
                    struct telos_error **error)
{
    struct telos_value *value = NULL;
    char path[OPENAI_CODEX_PATH_SIZE] = {0};
    char temporary[OPENAI_CODEX_PATH_SIZE] = {0};
    char json[OPENAI_CODEX_ACCESS_SIZE + OPENAI_CODEX_REFRESH_SIZE + 1024U];
    size_t written = 0;
    int descriptor = -1;
    bool result = false;

    if (!ensure_state_directory(authentication, error) ||
        !make_path(authentication, OPENAI_CODEX_CACHE_FILE, path, error) ||
        snprintf(temporary, sizeof(temporary), "%s/.openai-auth-XXXXXX",
                 authentication->state_directory) >=
            (int)sizeof(temporary)) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                      "OpenAI authentication temporary path is too long");
        }
        goto cleanup;
    }
    value = credentials_value(credentials, error);
    if (value == NULL ||
        !telos_value_write_json(value, json, sizeof(json), &written, error)) {
        goto cleanup;
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0 || fchmod(descriptor, 0600) != 0 ||
        !write_all(descriptor, json, written) || fsync(descriptor) != 0) {
        int saved_errno = errno;

        if (descriptor >= 0) {
            close(descriptor);
            descriptor = -1;
        }
        set_error(error, TELOS_ERROR_DOMAIN_IO, saved_errno,
                  "OpenAI authentication cache could not be written");
        goto cleanup;
    }
    if (close(descriptor) != 0) {
        int saved_errno = errno;

        descriptor = -1;
        set_error(error, TELOS_ERROR_DOMAIN_IO, saved_errno,
                  "OpenAI authentication cache could not be closed");
        goto cleanup;
    }
    descriptor = -1;
    if (rename(temporary, path) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "OpenAI authentication cache could not be replaced");
        goto cleanup;
    }
    temporary[0] = '\0';
    result = true;

cleanup:
    if (descriptor >= 0) {
        close(descriptor);
    }
    if (temporary[0] != '\0') {
        unlink(temporary);
    }
    secure_wipe(json, sizeof(json));
    telos_value_release(value);
    return result;
}

static bool read_file(int descriptor, char *buffer, size_t capacity,
                      size_t *size, struct telos_error **error)
{
    size_t used = 0;

    while (used + 1 < capacity) {
        ssize_t count = read(descriptor, buffer + used,
                             capacity - used - 1);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "OpenAI authentication cache could not be read");
            return false;
        }
        if (count == 0) {
            buffer[used] = '\0';
            *size = used;
            return true;
        }
        used += (size_t)count;
    }
    set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
              "OpenAI authentication cache is too large");
    return false;
}

static bool value_credentials(const struct telos_value *value,
                              struct oauth_credentials *credentials,
                              struct telos_error **error)
{
    const char *access = telos_value_string(
        telos_value_get(value, "access_token"));
    const char *refresh = telos_value_string(
        telos_value_get(value, "refresh_token"));
    const char *account = telos_value_string(
        telos_value_get(value, "account_id"));
    int64_t expires;

    if (telos_value_type(value) != TELOS_VALUE_OBJECT || access == NULL ||
        refresh == NULL || account == NULL || access[0] == '\0' ||
        refresh[0] == '\0' || account[0] == '\0' ||
        !telos_value_integer(telos_value_get(value, "expires_at"),
                             &expires) ||
        expires <= 0 ||
        !copy_text(credentials->access_token,
                   sizeof(credentials->access_token), access) ||
        !copy_text(credentials->refresh_token,
                   sizeof(credentials->refresh_token), refresh) ||
        !copy_text(credentials->account_id, sizeof(credentials->account_id),
                   account)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "OpenAI authentication cache is invalid");
        return false;
    }
    credentials->expires_at = expires;
    return true;
}

static bool load_credentials(struct telos_authentication *authentication,
                             struct telos_error **error)
{
    struct oauth_credentials credentials = {0};
    struct telos_value *value = NULL;
    struct stat opened_status;
    struct stat status;
    char path[OPENAI_CODEX_PATH_SIZE];
    char json[OPENAI_CODEX_ACCESS_SIZE + OPENAI_CODEX_REFRESH_SIZE + 1024U];
    size_t size = 0;
    int descriptor = -1;
    bool result = false;

    if (!make_path(authentication, OPENAI_CODEX_CACHE_FILE, path, error)) {
        goto cleanup;
    }
    if (lstat(path, &status) != 0) {
        if (errno == ENOENT) {
            result = true;
            goto cleanup;
        }
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "OpenAI authentication cache is unavailable");
        goto cleanup;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != getuid() ||
        (status.st_mode & 0077) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "OpenAI authentication cache permissions are unsafe");
        goto cleanup;
    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW);
    if (descriptor < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "OpenAI authentication cache could not be opened");
        goto cleanup;
    }
    if (fstat(descriptor, &opened_status) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "OpenAI authentication cache could not be inspected");
        goto cleanup;
    }
    if (opened_status.st_dev != status.st_dev ||
        opened_status.st_ino != status.st_ino ||
        !S_ISREG(opened_status.st_mode) ||
        opened_status.st_uid != getuid() ||
        (opened_status.st_mode & 0077) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "OpenAI authentication cache changed while opening");
        goto cleanup;
    }
    if (!read_file(descriptor, json, sizeof(json), &size, error)) {
        goto cleanup;
    }
    value = telos_value_parse_json(json, size, error);
    if (value == NULL || !value_credentials(value, &credentials, error)) {
        goto cleanup;
    }
    copy_text(authentication->access_token,
              sizeof(authentication->access_token), credentials.access_token);
    copy_text(authentication->refresh_token,
              sizeof(authentication->refresh_token),
              credentials.refresh_token);
    copy_text(authentication->account_id, sizeof(authentication->account_id),
              credentials.account_id);
    authentication->expires_at = credentials.expires_at;
    authentication->state = TELOS_AUTHENTICATION_SIGNED_IN;
    result = true;

cleanup:
    if (descriptor >= 0) {
        close(descriptor);
    }
    secure_wipe(json, sizeof(json));
    secure_wipe(&credentials, sizeof(credentials));
    telos_value_release(value);
    return result;
}

static bool receive_response(const char *data, size_t size, void *context,
                             struct telos_error **error)
{
    struct response_buffer *response = context;

    if (size > sizeof(response->data) - response->size - 1) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "OpenAI authentication response is too large");
        return false;
    }
    memcpy(response->data + response->size, data, size);
    response->size += size;
    response->data[response->size] = '\0';
    return true;
}

static bool send_request(struct telos_authentication *authentication,
                         const char *path, const char *content_type,
                         const char *body, const struct telos_cancel *cancel,
                         struct response_buffer *response, int *status_code,
                         struct telos_error **error)
{
    char url[OPENAI_CODEX_ENDPOINT_SIZE + 128U];
    const struct telos_transport_request request = {
        .method = "POST",
        .url = url,
        .content_type = content_type,
        .accept = "application/json",
        .body = body,
        .body_size = strlen(body),
        .cancel = cancel,
    };

    if (snprintf(url, sizeof(url), "%s%s", authentication->service_endpoint,
                 path) >= (int)sizeof(url)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "OpenAI authentication URL is too long");
        return false;
    }
    memset(response, 0, sizeof(*response));
    return authentication->send(&request, receive_response, response,
                                status_code,
                                authentication->transport_context, error);
}

static bool json_string(const struct telos_value *value, const char *key,
                        char *target, size_t capacity)
{
    return copy_text(target, capacity,
                     telos_value_string(telos_value_get(value, key)));
}

static bool parse_device_start(const struct response_buffer *response,
                               char *device_id, char *user_code,
                               unsigned int *interval,
                               struct telos_error **error)
{
    struct telos_value *value =
        telos_value_parse_json(response->data, response->size, error);
    const struct telos_value *interval_value;
    int64_t integer;
    bool result = false;

    if (value == NULL) {
        return false;
    }
    interval_value = telos_value_get(value, "interval");
    if (telos_value_integer(interval_value, &integer)) {
        result = integer >= 0 && integer <= 60;
    } else {
        const char *text = telos_value_string(interval_value);
        char *end = NULL;
        unsigned long parsed = text == NULL ? 0 : strtoul(text, &end, 10);

        result = text != NULL && text[0] != '\0' && end != NULL &&
                 *end == '\0' && parsed <= 60;
        integer = (int64_t)parsed;
    }
    result = result &&
             json_string(value, "device_auth_id", device_id,
                         OPENAI_CODEX_DEVICE_ID_SIZE) &&
             json_string(value, "user_code", user_code,
                         OPENAI_CODEX_USER_CODE_SIZE) &&
             device_id[0] != '\0' && user_code[0] != '\0';
    if (!result) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "OpenAI device authorization response is invalid");
    } else {
        *interval = (unsigned int)integer;
    }
    telos_value_release(value);
    return result;
}

static bool parse_device_token(const struct response_buffer *response,
                               char *authorization_code, char *verifier,
                               struct telos_error **error)
{
    struct telos_value *value =
        telos_value_parse_json(response->data, response->size, error);
    bool result;

    if (value == NULL) {
        return false;
    }
    result = json_string(value, "authorization_code", authorization_code,
                         OPENAI_CODEX_AUTHORIZATION_CODE_SIZE) &&
             json_string(value, "code_verifier", verifier,
                         OPENAI_CODEX_VERIFIER_SIZE) &&
             authorization_code[0] != '\0' && verifier[0] != '\0';
    if (!result) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "OpenAI device authorization token is invalid");
    }
    telos_value_release(value);
    return result;
}

static int base64_value(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '-' || value == '+') {
        return 62;
    }
    if (value == '_' || value == '/') {
        return 63;
    }
    return -1;
}

static bool decode_base64url(const char *input, size_t size, char *output,
                             size_t capacity, size_t *written)
{
    uint32_t bits = 0;
    unsigned int bit_count = 0;
    size_t used = 0;

    for (size_t index = 0; index < size; ++index) {
        int value = base64_value((unsigned char)input[index]);

        if (value < 0) {
            return false;
        }
        bits = (bits << 6) | (uint32_t)value;
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            if (used + 1 >= capacity) {
                return false;
            }
            output[used++] = (char)((bits >> bit_count) & 0xffU);
        }
    }
    output[used] = '\0';
    *written = used;
    return true;
}

static bool account_from_access_token(const char *access_token,
                                      char *account_id, size_t capacity,
                                      struct telos_error **error)
{
    const char *first = strchr(access_token, '.');
    const char *second = first == NULL ? NULL : strchr(first + 1, '.');
    char json[8192];
    size_t size;
    struct telos_value *root = NULL;
    const struct telos_value *auth;
    const char *account;
    bool result = false;

    if (first == NULL || second == NULL || first + 1 == second ||
        !decode_base64url(first + 1, (size_t)(second - first - 1), json,
                          sizeof(json), &size)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "OpenAI access token is invalid");
        goto cleanup;
    }
    root = telos_value_parse_json(json, size, error);
    if (root == NULL) {
        goto cleanup;
    }
    auth = telos_value_get(root, "https://api.openai.com/auth");
    account = telos_value_string(telos_value_get(auth,
                                                 "chatgpt_account_id"));
    if (account == NULL || account[0] == '\0' ||
        !copy_text(account_id, capacity, account)) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "OpenAI access token has no account identifier");
        goto cleanup;
    }
    result = true;

cleanup:
    secure_wipe(json, sizeof(json));
    telos_value_release(root);
    return result;
}

static bool parse_token_response(const struct response_buffer *response,
                                 const char *fallback_refresh,
                                 struct oauth_credentials *credentials,
                                 struct telos_error **error)
{
    struct telos_value *value =
        telos_value_parse_json(response->data, response->size, error);
    const char *access;
    const char *refresh;
    int64_t expires_in;
    time_t now;
    bool result = false;

    if (value == NULL) {
        return false;
    }
    access = telos_value_string(telos_value_get(value, "access_token"));
    refresh = telos_value_string(telos_value_get(value, "refresh_token"));
    if (refresh == NULL || refresh[0] == '\0') {
        refresh = fallback_refresh;
    }
    now = time(NULL);
    if (access == NULL || access[0] == '\0' || refresh == NULL ||
        refresh[0] == '\0' || now < 0 ||
        !telos_value_integer(telos_value_get(value, "expires_in"),
                             &expires_in) ||
        expires_in <= 0 || expires_in > INT64_MAX - (int64_t)now ||
        !copy_text(credentials->access_token,
                   sizeof(credentials->access_token), access) ||
        !copy_text(credentials->refresh_token,
                   sizeof(credentials->refresh_token), refresh) ||
        !account_from_access_token(access, credentials->account_id,
                                   sizeof(credentials->account_id), error)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "OpenAI token response is invalid");
        }
        goto cleanup;
    }
    credentials->expires_at = (int64_t)now + expires_in;
    result = true;

cleanup:
    telos_value_release(value);
    return result;
}

static bool form_character(unsigned char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_' ||
           value == '.' || value == '~';
}

static bool append_form(char *form, size_t capacity, size_t *used,
                        const char *key, const char *value)
{
    static const char hexadecimal[] = "0123456789ABCDEF";

    if (*used > 0) {
        if (*used + 1 >= capacity) {
            return false;
        }
        form[(*used)++] = '&';
    }
    for (size_t index = 0; key[index] != '\0'; ++index) {
        if (*used + 1 >= capacity) {
            return false;
        }
        form[(*used)++] = key[index];
    }
    if (*used + 1 >= capacity) {
        return false;
    }
    form[(*used)++] = '=';
    for (size_t index = 0; value[index] != '\0'; ++index) {
        unsigned char byte = (unsigned char)value[index];

        if (form_character(byte)) {
            if (*used + 1 >= capacity) {
                return false;
            }
            form[(*used)++] = (char)byte;
        } else {
            if (*used + 3 >= capacity) {
                return false;
            }
            form[(*used)++] = '%';
            form[(*used)++] = hexadecimal[byte >> 4];
            form[(*used)++] = hexadecimal[byte & 0x0fU];
        }
    }
    form[*used] = '\0';
    return true;
}

static bool exchange_code(struct telos_authentication *authentication,
                          const char *authorization_code,
                          const char *verifier,
                          const struct telos_cancel *cancel,
                          struct oauth_credentials *credentials,
                          struct telos_error **error)
{
    struct response_buffer response;
    char redirect_uri[OPENAI_CODEX_ENDPOINT_SIZE + 32U];
    char form[OPENAI_CODEX_FORM_SIZE] = {0};
    size_t used = 0;
    int status_code;
    bool result = false;

    if (snprintf(redirect_uri, sizeof(redirect_uri), "%s/deviceauth/callback",
                 authentication->service_endpoint) >=
            (int)sizeof(redirect_uri) ||
        !append_form(form, sizeof(form), &used, "grant_type",
                     "authorization_code") ||
        !append_form(form, sizeof(form), &used, "client_id",
                     OPENAI_CODEX_CLIENT_ID) ||
        !append_form(form, sizeof(form), &used, "code",
                     authorization_code) ||
        !append_form(form, sizeof(form), &used, "code_verifier", verifier) ||
        !append_form(form, sizeof(form), &used, "redirect_uri",
                     redirect_uri)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "OpenAI token exchange form is too large");
        goto cleanup;
    }
    if (!send_request(authentication, "/oauth/token",
                      "application/x-www-form-urlencoded", form, cancel,
                      &response, &status_code, error)) {
        goto cleanup;
    }
    if (status_code < 200 || status_code >= 300) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                  "OpenAI token exchange was rejected");
        goto cleanup;
    }
    result = parse_token_response(&response, NULL, credentials, error);

cleanup:
    secure_wipe(form, sizeof(form));
    secure_wipe(&response, sizeof(response));
    return result;
}

static bool wait_interval(unsigned int seconds,
                          const struct telos_cancel *cancel,
                          struct telos_error **error)
{
    const struct timespec delay = {
        .tv_nsec = 100000000L,
    };
    unsigned int ticks = seconds * 10U;

    for (unsigned int tick = 0; tick < ticks; ++tick) {
        struct timespec remaining = delay;

        if (telos_cancel_requested(cancel)) {
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "OpenAI login was cancelled");
            return false;
        }
        while (nanosleep(&remaining, &remaining) != 0) {
            if (errno != EINTR) {
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "OpenAI login wait failed");
                return false;
            }
            if (telos_cancel_requested(cancel)) {
                set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                          "OpenAI login was cancelled");
                return false;
            }
        }
    }
    return true;
}

static bool emit_authentication(telos_authentication_event_fn emit,
                                void *emit_context,
                                enum telos_authentication_event_kind kind,
                                const char *verification_uri,
                                const char *user_code,
                                struct telos_error **error)
{
    const struct telos_authentication_event event = {
        .kind = kind,
        .verification_uri = verification_uri,
        .user_code = user_code,
    };

    return emit(&event, emit_context, error);
}

static struct telos_authentication *
openai_auth_create(const struct telos_authentication_config *config,
                   struct telos_error **error)
{
    struct telos_authentication *authentication;
    const char *endpoint;
    size_t endpoint_size;

    if (error != NULL) {
        *error = NULL;
    }
    endpoint = config == NULL || config->service_endpoint == NULL
                   ? TELOS_OPENAI_CODEX_AUTH_ENDPOINT
                   : config->service_endpoint;
    if (config == NULL || config->state_directory == NULL ||
        config->state_directory[0] != '/' || config->send == NULL ||
        !endpoint_allowed(endpoint)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "OpenAI authentication configuration is invalid");
        return NULL;
    }
    authentication = calloc(1, sizeof(*authentication));
    if (authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "OpenAI authentication allocation failed");
        return NULL;
    }
    endpoint_size = strlen(endpoint);
    while (endpoint_size > 8 && endpoint[endpoint_size - 1] == '/') {
        --endpoint_size;
    }
    if (strlen(config->state_directory) >=
            sizeof(authentication->state_directory) ||
        endpoint_size >= sizeof(authentication->service_endpoint)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "OpenAI authentication configuration is too long");
        free(authentication);
        return NULL;
    }
    memcpy(authentication->state_directory, config->state_directory,
           strlen(config->state_directory) + 1);
    memcpy(authentication->service_endpoint, endpoint, endpoint_size);
    authentication->service_endpoint[endpoint_size] = '\0';
    authentication->send = config->send;
    authentication->transport_context = config->transport_context;
    authentication->state = TELOS_AUTHENTICATION_SIGNED_OUT;
    if (!ensure_state_directory(authentication, error) ||
        !load_credentials(authentication, error)) {
        secure_wipe(authentication, sizeof(*authentication));
        free(authentication);
        return NULL;
    }
    return authentication;
}

static void openai_auth_destroy(struct telos_authentication *authentication)
{
    if (authentication != NULL) {
        secure_wipe(authentication, sizeof(*authentication));
        free(authentication);
    }
}

static bool openai_auth_login(struct telos_authentication *authentication,
                              const struct telos_cancel *cancel,
                              telos_authentication_event_fn emit,
                              void *emit_context,
                              struct telos_error **error)
{
    struct oauth_credentials credentials = {0};
    struct response_buffer response;
    char body[OPENAI_CODEX_DEVICE_ID_SIZE + OPENAI_CODEX_USER_CODE_SIZE + 64U];
    char device_id[OPENAI_CODEX_DEVICE_ID_SIZE] = {0};
    char user_code[OPENAI_CODEX_USER_CODE_SIZE] = {0};
    char authorization_code[OPENAI_CODEX_AUTHORIZATION_CODE_SIZE] = {0};
    char verifier[OPENAI_CODEX_VERIFIER_SIZE] = {0};
    char verification_uri[OPENAI_CODEX_ENDPOINT_SIZE + 32U];
    enum telos_authentication_state prior_state;
    time_t deadline;
    unsigned int interval;
    int status_code;
    bool committed = false;
    bool result = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (authentication == NULL || emit == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "OpenAI login arguments are invalid");
        return false;
    }
    prior_state = authentication->state == TELOS_AUTHENTICATION_SIGNED_IN
                      ? TELOS_AUTHENTICATION_SIGNED_IN
                      : TELOS_AUTHENTICATION_SIGNED_OUT;
    authentication->state = TELOS_AUTHENTICATION_AUTHORIZING;
    if (snprintf(body, sizeof(body), "{\"client_id\":\"%s\"}",
                 OPENAI_CODEX_CLIENT_ID) >= (int)sizeof(body) ||
        !send_request(authentication, "/api/accounts/deviceauth/usercode",
                      "application/json", body, cancel, &response,
                      &status_code, error)) {
        goto cleanup;
    }
    if (status_code < 200 || status_code >= 300 ||
        !parse_device_start(&response, device_id, user_code, &interval,
                            error)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                      "OpenAI device login is unavailable");
        }
        goto cleanup;
    }
    if (endpoint_is_loopback(authentication->service_endpoint)) {
        interval = 0;
    } else if (interval == 0) {
        interval = 1;
    }
    if (snprintf(verification_uri, sizeof(verification_uri), "%s/codex/device",
                 authentication->service_endpoint) >=
            (int)sizeof(verification_uri) ||
        !emit_authentication(emit, emit_context,
                             TELOS_AUTHENTICATION_VERIFICATION_REQUIRED,
                             verification_uri, user_code, error)) {
        goto cleanup;
    }
    deadline = time(NULL);
    if (deadline < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "OpenAI login clock is unavailable");
        goto cleanup;
    }
    deadline += OPENAI_CODEX_LOGIN_TIMEOUT_SECONDS;
    for (;;) {
        time_t now = time(NULL);

        if (now < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "OpenAI login clock is unavailable");
            goto cleanup;
        }
        if (now > deadline) {
            break;
        }
        if (telos_cancel_requested(cancel)) {
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "OpenAI login was cancelled");
            goto cleanup;
        }
        if (snprintf(body, sizeof(body),
                     "{\"device_auth_id\":\"%s\",\"user_code\":\"%s\"}",
                     device_id, user_code) >= (int)sizeof(body)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                      "OpenAI device login request is too large");
            goto cleanup;
        }
        if (!send_request(authentication, "/api/accounts/deviceauth/token",
                          "application/json", body, cancel, &response,
                          &status_code, error)) {
            goto cleanup;
        }
        if (status_code >= 200 && status_code < 300) {
            if (!parse_device_token(&response, authorization_code, verifier,
                                    error)) {
                goto cleanup;
            }
            break;
        }
        if (status_code != 403 && status_code != 404) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                      "OpenAI device login was rejected");
            goto cleanup;
        }
        if (!wait_interval(interval, cancel, error)) {
            goto cleanup;
        }
    }
    if (authorization_code[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_IO, ETIMEDOUT,
                  "OpenAI device login timed out");
        goto cleanup;
    }
    if (!exchange_code(authentication, authorization_code, verifier, cancel,
                       &credentials, error) ||
        !persist_credentials(authentication, &credentials, error)) {
        goto cleanup;
    }
    secure_wipe(authentication->access_token,
                sizeof(authentication->access_token));
    secure_wipe(authentication->refresh_token,
                sizeof(authentication->refresh_token));
    copy_text(authentication->access_token,
              sizeof(authentication->access_token), credentials.access_token);
    copy_text(authentication->refresh_token,
              sizeof(authentication->refresh_token),
              credentials.refresh_token);
    copy_text(authentication->account_id, sizeof(authentication->account_id),
              credentials.account_id);
    authentication->expires_at = credentials.expires_at;
    authentication->state = TELOS_AUTHENTICATION_SIGNED_IN;
    committed = true;
    result = emit_authentication(emit, emit_context,
                                 TELOS_AUTHENTICATION_COMPLETED, NULL, NULL,
                                 error);

cleanup:
    if (!committed) {
        authentication->state = prior_state;
    }
    secure_wipe(&credentials, sizeof(credentials));
    secure_wipe(&response, sizeof(response));
    secure_wipe(body, sizeof(body));
    secure_wipe(device_id, sizeof(device_id));
    secure_wipe(user_code, sizeof(user_code));
    secure_wipe(authorization_code, sizeof(authorization_code));
    secure_wipe(verifier, sizeof(verifier));
    return result;
}

static bool openai_auth_logout(struct telos_authentication *authentication,
                               struct telos_error **error)
{
    char path[OPENAI_CODEX_PATH_SIZE];

    if (error != NULL) {
        *error = NULL;
    }
    if (authentication == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "OpenAI logout arguments are invalid");
        return false;
    }
    if (!make_path(authentication, OPENAI_CODEX_CACHE_FILE, path, error)) {
        return false;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "OpenAI authentication cache could not be removed");
        return false;
    }
    secure_wipe(authentication->access_token,
                sizeof(authentication->access_token));
    secure_wipe(authentication->refresh_token,
                sizeof(authentication->refresh_token));
    secure_wipe(authentication->account_id,
                sizeof(authentication->account_id));
    authentication->expires_at = 0;
    authentication->state = TELOS_AUTHENTICATION_SIGNED_OUT;
    return true;
}

static bool
openai_auth_status(const struct telos_authentication *authentication,
                   struct telos_authentication_status *status,
                   struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (authentication == NULL || status == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "OpenAI authentication status arguments are invalid");
        return false;
    }
    *status = (struct telos_authentication_status){
        .state = authentication->state,
        .provider = OPENAI_CODEX_PROVIDER,
        .account_id = authentication->state == TELOS_AUTHENTICATION_SIGNED_IN
                          ? authentication->account_id
                          : NULL,
    };
    return true;
}

static bool refresh_credentials(struct telos_authentication *authentication,
                                struct telos_error **error)
{
    struct oauth_credentials credentials = {0};
    struct response_buffer response;
    char form[OPENAI_CODEX_FORM_SIZE] = {0};
    size_t used = 0;
    int status_code;
    bool result = false;

    if (!append_form(form, sizeof(form), &used, "grant_type",
                     "refresh_token") ||
        !append_form(form, sizeof(form), &used, "refresh_token",
                     authentication->refresh_token) ||
        !append_form(form, sizeof(form), &used, "client_id",
                     OPENAI_CODEX_CLIENT_ID)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EFBIG,
                  "OpenAI token refresh form is too large");
        goto cleanup;
    }
    if (!send_request(authentication, "/oauth/token",
                      "application/x-www-form-urlencoded", form, NULL,
                      &response, &status_code, error)) {
        goto cleanup;
    }
    if (status_code < 200 || status_code >= 300) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, status_code,
                  "OpenAI token refresh was rejected");
        goto cleanup;
    }
    if (!parse_token_response(&response, authentication->refresh_token,
                              &credentials, error) ||
        !persist_credentials(authentication, &credentials, error)) {
        goto cleanup;
    }
    secure_wipe(authentication->access_token,
                sizeof(authentication->access_token));
    secure_wipe(authentication->refresh_token,
                sizeof(authentication->refresh_token));
    copy_text(authentication->access_token,
              sizeof(authentication->access_token), credentials.access_token);
    copy_text(authentication->refresh_token,
              sizeof(authentication->refresh_token),
              credentials.refresh_token);
    copy_text(authentication->account_id, sizeof(authentication->account_id),
              credentials.account_id);
    authentication->expires_at = credentials.expires_at;
    result = true;

cleanup:
    secure_wipe(&credentials, sizeof(credentials));
    secure_wipe(&response, sizeof(response));
    secure_wipe(form, sizeof(form));
    return result;
}

static char *
openai_auth_resolve(struct telos_authentication *authentication,
                    const char *target, struct telos_error **error)
{
    time_t now;
    char *secret;

    if (error != NULL) {
        *error = NULL;
    }
    if (authentication == NULL || target == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "OpenAI secret resolution arguments are invalid");
        return NULL;
    }
    if (strcmp(target, OPENAI_CODEX_SECRET_TARGET) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                  "OpenAI credential is not authorized for this target");
        return NULL;
    }
    if (authentication->state != TELOS_AUTHENTICATION_SIGNED_IN) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, ENOENT,
                  "OpenAI is not logged in; run /login first");
        return NULL;
    }
    now = time(NULL);
    if (now < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "OpenAI authentication clock is unavailable");
        return NULL;
    }
    if ((int64_t)now + OPENAI_CODEX_REFRESH_SKEW_SECONDS >=
            authentication->expires_at &&
        !refresh_credentials(authentication, error)) {
        return NULL;
    }
    secret = duplicate_secret(authentication->access_token);
    if (secret == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "OpenAI credential allocation failed");
    }
    return secret;
}

static const struct telos_authentication_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = TELOS_OPENAI_CODEX_AUTH_ID,
    .create = openai_auth_create,
    .destroy = openai_auth_destroy,
    .login = openai_auth_login,
    .logout = openai_auth_logout,
    .status = openai_auth_status,
    .resolve = openai_auth_resolve,
};

const struct telos_authentication_definition_v1 *
telos_openai_codex_authentication_definition(void)
{
    return &definition;
}
