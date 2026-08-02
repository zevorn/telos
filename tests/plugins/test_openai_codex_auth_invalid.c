#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/authentication.h>
#include <telos/cancel.h>
#include <telos/plugins/openai_codex_auth.h>

#define TEST_PATH_SIZE 4096U

static const char access_token[] =
    "e30."
    "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
    "X2lkIjoiYWNjdC1mYWlsdXJlIn19.signature";

enum transport_mode {
    TRANSPORT_NORMAL = 1,
    TRANSPORT_USER_SEND_FAILURE,
    TRANSPORT_USER_STATUS_FAILURE,
    TRANSPORT_USER_INVALID_JSON,
    TRANSPORT_USER_INTERVAL_HIGH,
    TRANSPORT_USER_INTERVAL_TEXT,
    TRANSPORT_USER_INTERVAL_EMPTY,
    TRANSPORT_USER_INTERVAL_INVALID,
    TRANSPORT_USER_INTERVAL_TEXT_HIGH,
    TRANSPORT_USER_MISSING_DEVICE,
    TRANSPORT_USER_EMPTY_DEVICE,
    TRANSPORT_USER_MISSING_CODE,
    TRANSPORT_USER_EMPTY_CODE,
    TRANSPORT_USER_OVERSIZED,
    TRANSPORT_DEVICE_SEND_FAILURE,
    TRANSPORT_DEVICE_STATUS_FAILURE,
    TRANSPORT_DEVICE_INVALID_JSON,
    TRANSPORT_DEVICE_MISSING_CODE,
    TRANSPORT_DEVICE_EMPTY_CODE,
    TRANSPORT_DEVICE_MISSING_VERIFIER,
    TRANSPORT_DEVICE_EMPTY_VERIFIER,
    TRANSPORT_DEVICE_PENDING_CANCEL,
    TRANSPORT_DEVICE_PENDING_ONCE,
    TRANSPORT_EXCHANGE_SEND_FAILURE,
    TRANSPORT_EXCHANGE_STATUS_FAILURE,
    TRANSPORT_TOKEN_INVALID_JSON,
    TRANSPORT_TOKEN_MISSING_ACCESS,
    TRANSPORT_TOKEN_EMPTY_ACCESS,
    TRANSPORT_TOKEN_MISSING_REFRESH,
    TRANSPORT_TOKEN_BAD_EXPIRES,
    TRANSPORT_TOKEN_OVERFLOW_EXPIRES,
    TRANSPORT_TOKEN_NO_DOTS,
    TRANSPORT_TOKEN_ONE_DOT,
    TRANSPORT_TOKEN_EMPTY_PAYLOAD,
    TRANSPORT_TOKEN_BAD_BASE64,
    TRANSPORT_TOKEN_DASH_PAYLOAD,
    TRANSPORT_TOKEN_UNDERSCORE_PAYLOAD,
    TRANSPORT_TOKEN_PLUS_PAYLOAD,
    TRANSPORT_TOKEN_SLASH_PAYLOAD,
    TRANSPORT_TOKEN_NON_JSON_PAYLOAD,
    TRANSPORT_TOKEN_EMPTY_JSON_PAYLOAD,
    TRANSPORT_REFRESH_SEND_FAILURE,
    TRANSPORT_REFRESH_STATUS_FAILURE,
    TRANSPORT_REFRESH_INVALID_JSON,
    TRANSPORT_REFRESH_WITHOUT_REPLACEMENT,
};

enum event_mode {
    EVENT_ACCEPT = 1,
    EVENT_REJECT_VERIFICATION,
    EVENT_REJECT_COMPLETION,
};

struct transport_fixture {
    enum transport_mode mode;
    struct telos_cancel *cancel;
    size_t device_requests;
};

struct event_fixture {
    enum event_mode mode;
    size_t verification_events;
    size_t completion_events;
};

typedef struct telos_authentication_definition_v1 authentication_definition;

static void set_fixture_error(struct telos_error **error,
                              const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(TELOS_ERROR_DOMAIN_IO, EIO, message,
                                    NULL);
    }
}

static bool send_json(telos_transport_chunk_fn receive,
                      void *receive_context,
                      int *status_code,
                      int status,
                      const char *json,
                      struct telos_error **error)
{
    *status_code = status;
    return receive(json, strlen(json), receive_context, error);
}

static bool send_token(telos_transport_chunk_fn receive,
                       void *receive_context,
                       int *status_code,
                       const char *token,
                       bool include_refresh,
                       int expires,
                       struct telos_error **error)
{
    char response[2048];

    if (include_refresh) {
        assert(snprintf(response, sizeof(response),
                        "{\"access_token\":\"%s\","
                        "\"refresh_token\":\"refresh-failure\","
                        "\"expires_in\":%d}", token, expires) <
               (int)sizeof(response));
    } else {
        assert(snprintf(response, sizeof(response),
                        "{\"access_token\":\"%s\","
                        "\"expires_in\":%d}", token, expires) <
               (int)sizeof(response));
    }
    return send_json(receive, receive_context, status_code, 200, response,
                     error);
}

static bool send_user_code(struct transport_fixture *fixture,
                           telos_transport_chunk_fn receive,
                           void *receive_context,
                           int *status_code,
                           struct telos_error **error)
{
    static char oversized[70U * 1024U];

    switch (fixture->mode) {
    case TRANSPORT_USER_SEND_FAILURE:
        set_fixture_error(error, "user-code transport failed");
        return false;
    case TRANSPORT_USER_STATUS_FAILURE:
        return send_json(receive, receive_context, status_code, 503, "{}",
                         error);
    case TRANSPORT_USER_INVALID_JSON:
        return send_json(receive, receive_context, status_code, 200, "{",
                         error);
    case TRANSPORT_USER_INTERVAL_HIGH:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":61}",
                         error);
    case TRANSPORT_USER_INTERVAL_TEXT:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":\"0\"}",
                         error);
    case TRANSPORT_USER_INTERVAL_EMPTY:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":\"\"}",
                         error);
    case TRANSPORT_USER_INTERVAL_INVALID:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":\"x\"}",
                         error);
    case TRANSPORT_USER_INTERVAL_TEXT_HIGH:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":\"61\"}",
                         error);
    case TRANSPORT_USER_MISSING_DEVICE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"user_code\":\"CODE\",\"interval\":0}",
                         error);
    case TRANSPORT_USER_EMPTY_DEVICE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"\","
                         "\"user_code\":\"CODE\",\"interval\":0}",
                         error);
    case TRANSPORT_USER_MISSING_CODE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\",\"interval\":0}",
                         error);
    case TRANSPORT_USER_EMPTY_CODE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"\",\"interval\":0}",
                         error);
    case TRANSPORT_USER_OVERSIZED:
        memset(oversized, 'x', sizeof(oversized));
        *status_code = 200;
        return receive(oversized, sizeof(oversized), receive_context, error);
    default:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device\","
                         "\"user_code\":\"CODE\",\"interval\":0}",
                         error);
    }
}

static bool send_device_token(struct transport_fixture *fixture,
                              telos_transport_chunk_fn receive,
                              void *receive_context,
                              int *status_code,
                              struct telos_error **error)
{
    fixture->device_requests += 1;
    switch (fixture->mode) {
    case TRANSPORT_USER_INTERVAL_TEXT:
    case TRANSPORT_DEVICE_STATUS_FAILURE:
        return send_json(receive, receive_context, status_code, 500, "{}",
                         error);
    case TRANSPORT_DEVICE_SEND_FAILURE:
        set_fixture_error(error, "device-token transport failed");
        return false;
    case TRANSPORT_DEVICE_INVALID_JSON:
        return send_json(receive, receive_context, status_code, 200, "{",
                         error);
    case TRANSPORT_DEVICE_MISSING_CODE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"code_verifier\":\"verifier\"}", error);
    case TRANSPORT_DEVICE_EMPTY_CODE:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"authorization_code\":\"\","
                         "\"code_verifier\":\"verifier\"}", error);
    case TRANSPORT_DEVICE_MISSING_VERIFIER:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"authorization_code\":\"authorization\"}",
                         error);
    case TRANSPORT_DEVICE_EMPTY_VERIFIER:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"authorization_code\":\"authorization\","
                         "\"code_verifier\":\"\"}", error);
    case TRANSPORT_DEVICE_PENDING_CANCEL:
        assert(fixture->cancel != NULL);
        assert(telos_cancel_request(fixture->cancel));
        return send_json(receive, receive_context, status_code, 403, "{}",
                         error);
    case TRANSPORT_DEVICE_PENDING_ONCE:
        if (fixture->device_requests == 1) {
            return send_json(receive, receive_context, status_code, 404,
                             "{}", error);
        }
        break;
    default:
        break;
    }
    return send_json(receive, receive_context, status_code, 200,
                     "{\"authorization_code\":\"authorization\","
                     "\"code_verifier\":\"verifier\"}", error);
}

static bool send_exchange(struct transport_fixture *fixture,
                          telos_transport_chunk_fn receive,
                          void *receive_context,
                          int *status_code,
                          struct telos_error **error)
{
    switch (fixture->mode) {
    case TRANSPORT_EXCHANGE_SEND_FAILURE:
        set_fixture_error(error, "token exchange transport failed");
        return false;
    case TRANSPORT_EXCHANGE_STATUS_FAILURE:
        return send_json(receive, receive_context, status_code, 401, "{}",
                         error);
    case TRANSPORT_TOKEN_INVALID_JSON:
        return send_json(receive, receive_context, status_code, 200, "{",
                         error);
    case TRANSPORT_TOKEN_MISSING_ACCESS:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"refresh_token\":\"refresh\","
                         "\"expires_in\":3600}", error);
    case TRANSPORT_TOKEN_EMPTY_ACCESS:
        return send_json(receive, receive_context, status_code, 200,
                         "{\"access_token\":\"\","
                         "\"refresh_token\":\"refresh\","
                         "\"expires_in\":3600}", error);
    case TRANSPORT_TOKEN_MISSING_REFRESH:
        return send_token(receive, receive_context, status_code, access_token,
                          false, 3600, error);
    case TRANSPORT_TOKEN_BAD_EXPIRES:
        return send_token(receive, receive_context, status_code, access_token,
                          true, 0, error);
    case TRANSPORT_TOKEN_OVERFLOW_EXPIRES:
        {
            char response[2048];

            assert(snprintf(response, sizeof(response),
                            "{\"access_token\":\"%s\","
                            "\"refresh_token\":\"refresh\","
                            "\"expires_in\":9223372036854775807}",
                            access_token) < (int)sizeof(response));
            return send_json(receive, receive_context, status_code, 200,
                             response, error);
        }
    case TRANSPORT_TOKEN_NO_DOTS:
        return send_token(receive, receive_context, status_code, "invalid",
                          true, 3600, error);
    case TRANSPORT_TOKEN_ONE_DOT:
        return send_token(receive, receive_context, status_code, "a.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_EMPTY_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a..b", true,
                          3600, error);
    case TRANSPORT_TOKEN_BAD_BASE64:
        return send_token(receive, receive_context, status_code, "a.!.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_DASH_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a.-.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_UNDERSCORE_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a._.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_PLUS_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a.+.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_SLASH_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a./.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_NON_JSON_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a.YQ.b", true,
                          3600, error);
    case TRANSPORT_TOKEN_EMPTY_JSON_PAYLOAD:
        return send_token(receive, receive_context, status_code, "a.e30.b",
                          true, 3600, error);
    default:
        return send_token(receive, receive_context, status_code, access_token,
                          true, 3600, error);
    }
}

static bool send_refresh(struct transport_fixture *fixture,
                         telos_transport_chunk_fn receive,
                         void *receive_context,
                         int *status_code,
                         struct telos_error **error)
{
    switch (fixture->mode) {
    case TRANSPORT_REFRESH_SEND_FAILURE:
        set_fixture_error(error, "token refresh transport failed");
        return false;
    case TRANSPORT_REFRESH_STATUS_FAILURE:
        return send_json(receive, receive_context, status_code, 401, "{}",
                         error);
    case TRANSPORT_REFRESH_INVALID_JSON:
        return send_json(receive, receive_context, status_code, 200, "{",
                         error);
    case TRANSPORT_REFRESH_WITHOUT_REPLACEMENT:
        return send_token(receive, receive_context, status_code, access_token,
                          false, 3600, error);
    default:
        return send_token(receive, receive_context, status_code, access_token,
                          true, 3600, error);
    }
}

static bool fake_send(const struct telos_transport_request *request,
                      telos_transport_chunk_fn receive,
                      void *receive_context,
                      int *status_code,
                      void *transport_context,
                      struct telos_error **error)
{
    struct transport_fixture *fixture = transport_context;

    assert(request != NULL);
    assert(strcmp(request->method, "POST") == 0);
    assert(strcmp(request->accept, "application/json") == 0);
    if (strstr(request->url, "/api/accounts/deviceauth/usercode") != NULL) {
        return send_user_code(fixture, receive, receive_context, status_code,
                              error);
    }
    if (strstr(request->url, "/api/accounts/deviceauth/token") != NULL) {
        return send_device_token(fixture, receive, receive_context,
                                 status_code, error);
    }
    if (strstr(request->url, "/oauth/token") != NULL &&
        strstr(request->body, "grant_type=refresh_token") != NULL) {
        return send_refresh(fixture, receive, receive_context, status_code,
                            error);
    }
    if (strstr(request->url, "/oauth/token") != NULL) {
        return send_exchange(fixture, receive, receive_context, status_code,
                             error);
    }
    abort();
}

static bool capture_event(const struct telos_authentication_event *event,
                          void *context,
                          struct telos_error **error)
{
    struct event_fixture *fixture = context;

    assert(event != NULL);
    if (event->kind == TELOS_AUTHENTICATION_VERIFICATION_REQUIRED) {
        fixture->verification_events += 1;
        if (fixture->mode == EVENT_REJECT_VERIFICATION) {
            set_fixture_error(error, "verification event rejected");
            return false;
        }
        return true;
    }
    assert(event->kind == TELOS_AUTHENTICATION_COMPLETED);
    fixture->completion_events += 1;
    if (fixture->mode == EVENT_REJECT_COMPLETION) {
        set_fixture_error(error, "completion event rejected");
        return false;
    }
    return true;
}

static void release_error(struct telos_error **error)
{
    assert(error != NULL);
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

static void
expect_create_error(const authentication_definition *definition,
                    const struct telos_authentication_config *config)
{
    struct telos_error *error = NULL;

    assert(definition->create(config, &error) == NULL);
    release_error(&error);
}

static void write_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(descriptor, data, size);

        assert(written > 0);
        data += (size_t)written;
        size -= (size_t)written;
    }
}

static void write_cache(const char *path, const char *data, size_t size,
                        mode_t mode)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);

    assert(descriptor >= 0);
    assert(fchmod(descriptor, mode) == 0);
    write_all(descriptor, data, size);
    assert(close(descriptor) == 0);
}

static void
test_create_and_cache_failures(const authentication_definition *definition)
{
    char directory[] = "/tmp/telos-auth-invalid-create-XXXXXX";
    char cache_path[TEST_PATH_SIZE];
    char file_path[TEST_PATH_SIZE];
    char missing_path[TEST_PATH_SIZE];
    char long_directory[5000];
    char long_endpoint[700];
    char large_value[18000];
    char document[20000];
    static char oversized[30U * 1024U];
    struct transport_fixture fixture = {
        .mode = TRANSPORT_NORMAL,
    };
    struct telos_authentication_config config = {
        .state_directory = directory,
        .service_endpoint = TELOS_OPENAI_CODEX_AUTH_ENDPOINT,
        .send = fake_send,
        .transport_context = &fixture,
    };
    struct telos_authentication *authentication;
    struct telos_error *error = NULL;

    assert(mkdtemp(directory) != NULL);
    assert(snprintf(cache_path, sizeof(cache_path), "%s/%s", directory,
                    "openai-codex-auth.json") < (int)sizeof(cache_path));
    assert(snprintf(file_path, sizeof(file_path), "%s/state-file", directory) <
           (int)sizeof(file_path));
    assert(snprintf(missing_path, sizeof(missing_path), "%s/missing/state",
                    directory) < (int)sizeof(missing_path));

    assert(chmod(directory, 0755) == 0);
    expect_create_error(definition, &config);
    assert(chmod(directory, 0700) == 0);

    assert(definition->create(NULL, NULL) == NULL);
    config.state_directory = NULL;
    expect_create_error(definition, &config);
    config.state_directory = "relative";
    expect_create_error(definition, &config);
    config.state_directory = directory;
    config.send = NULL;
    expect_create_error(definition, &config);
    config.send = fake_send;
    config.service_endpoint = "ftp://auth.openai.com";
    expect_create_error(definition, &config);
    config.service_endpoint = "https://example.invalid";
    expect_create_error(definition, &config);
    config.service_endpoint = "https://auth.openai.com.example.invalid";
    expect_create_error(definition, &config);

    config.service_endpoint = NULL;
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);
    config.service_endpoint = "https://auth.openai.com///";
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);
    config.service_endpoint = "https://localhost:8443";
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);
    config.service_endpoint = "http://localhost";
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);
    config.service_endpoint = "http://127.0.0.1/path";
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);
    config.service_endpoint = "http://[::1]:8080";
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    definition->destroy(authentication);

    memset(long_directory, 'x', sizeof(long_directory));
    long_directory[0] = '/';
    long_directory[sizeof(long_directory) - 1] = '\0';
    config.state_directory = long_directory;
    config.service_endpoint = TELOS_OPENAI_CODEX_AUTH_ENDPOINT;
    expect_create_error(definition, &config);
    memset(long_endpoint, 'x', sizeof(long_endpoint));
    memcpy(long_endpoint, "http://127.0.0.1/", 17);
    long_endpoint[sizeof(long_endpoint) - 1] = '\0';
    config.state_directory = directory;
    config.service_endpoint = long_endpoint;
    expect_create_error(definition, &config);

    write_cache(file_path, "x", 1, 0600);
    config.state_directory = file_path;
    config.service_endpoint = TELOS_OPENAI_CODEX_AUTH_ENDPOINT;
    expect_create_error(definition, &config);
    assert(unlink(file_path) == 0);
    config.state_directory = missing_path;
    expect_create_error(definition, &config);
    config.state_directory = directory;

    assert(mkdir(cache_path, 0700) == 0);
    expect_create_error(definition, &config);
    assert(rmdir(cache_path) == 0);
    assert(symlink("/dev/null", cache_path) == 0);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);
    write_cache(cache_path, "{}", 2, 0644);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);
    write_cache(cache_path, "{", 1, 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);
    write_cache(cache_path, "1", 1, 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);

    {
        const char *invalid_documents[] = {
            "{\"refresh_token\":\"r\",\"account_id\":\"a\","
            "\"expires_at\":1}",
            "{\"access_token\":\"\",\"refresh_token\":\"r\","
            "\"account_id\":\"a\",\"expires_at\":1}",
            "{\"access_token\":\"a\",\"account_id\":\"a\","
            "\"expires_at\":1}",
            "{\"access_token\":\"a\",\"refresh_token\":\"\","
            "\"account_id\":\"a\",\"expires_at\":1}",
            "{\"access_token\":\"a\",\"refresh_token\":\"r\","
            "\"expires_at\":1}",
            "{\"access_token\":\"a\",\"refresh_token\":\"r\","
            "\"account_id\":\"\",\"expires_at\":1}",
            "{\"access_token\":\"a\",\"refresh_token\":\"r\","
            "\"account_id\":\"a\"}",
            "{\"access_token\":\"a\",\"refresh_token\":\"r\","
            "\"account_id\":\"a\",\"expires_at\":0}",
        };

        for (size_t index = 0;
             index < sizeof(invalid_documents) / sizeof(invalid_documents[0]);
             ++index) {
            write_cache(cache_path, invalid_documents[index],
                        strlen(invalid_documents[index]), 0600);
            expect_create_error(definition, &config);
            assert(unlink(cache_path) == 0);
        }
    }

    memset(large_value, 'a', sizeof(large_value) - 1);
    large_value[sizeof(large_value) - 1] = '\0';
    assert(snprintf(document, sizeof(document),
                    "{\"access_token\":\"%s\","
                    "\"refresh_token\":\"r\",\"account_id\":\"a\","
                    "\"expires_at\":1}", large_value) <
           (int)sizeof(document));
    write_cache(cache_path, document, strlen(document), 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);

    large_value[9000] = '\0';
    assert(snprintf(document, sizeof(document),
                    "{\"access_token\":\"a\","
                    "\"refresh_token\":\"%s\",\"account_id\":\"a\","
                    "\"expires_at\":1}", large_value) <
           (int)sizeof(document));
    write_cache(cache_path, document, strlen(document), 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);

    large_value[300] = '\0';
    assert(snprintf(document, sizeof(document),
                    "{\"access_token\":\"a\","
                    "\"refresh_token\":\"r\",\"account_id\":\"%s\","
                    "\"expires_at\":1}", large_value) <
           (int)sizeof(document));
    write_cache(cache_path, document, strlen(document), 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);

    memset(oversized, 'x', sizeof(oversized));
    write_cache(cache_path, oversized, sizeof(oversized), 0600);
    expect_create_error(definition, &config);
    assert(unlink(cache_path) == 0);
    assert(rmdir(directory) == 0);
}

static void
expect_login_failure(const authentication_definition *definition,
                     struct telos_authentication *authentication,
                     struct transport_fixture *transport,
                     enum transport_mode transport_mode,
                     enum event_mode event_mode,
                     struct telos_cancel *cancel,
                     enum telos_authentication_state expected_state)
{
    struct event_fixture events = {
        .mode = event_mode,
    };
    struct telos_authentication_status status;
    struct telos_error *error = NULL;

    transport->mode = transport_mode;
    transport->cancel = cancel;
    transport->device_requests = 0;
    assert(!definition->login(authentication, cancel, capture_event, &events,
                              &error));
    release_error(&error);
    assert(definition->status(authentication, &status, &error));
    assert(error == NULL);
    assert(status.state == expected_state);
}

static void
test_login_failures(const authentication_definition *definition)
{
    char directory[] = "/tmp/telos-auth-invalid-login-XXXXXX";
    struct transport_fixture transport = {
        .mode = TRANSPORT_NORMAL,
    };
    const struct telos_authentication_config config = {
        .state_directory = directory,
        .service_endpoint = TELOS_OPENAI_CODEX_AUTH_ENDPOINT,
        .send = fake_send,
        .transport_context = &transport,
    };
    const enum transport_mode failures[] = {
        TRANSPORT_USER_SEND_FAILURE,
        TRANSPORT_USER_STATUS_FAILURE,
        TRANSPORT_USER_INVALID_JSON,
        TRANSPORT_USER_INTERVAL_HIGH,
        TRANSPORT_USER_INTERVAL_TEXT,
        TRANSPORT_USER_INTERVAL_EMPTY,
        TRANSPORT_USER_INTERVAL_INVALID,
        TRANSPORT_USER_INTERVAL_TEXT_HIGH,
        TRANSPORT_USER_MISSING_DEVICE,
        TRANSPORT_USER_EMPTY_DEVICE,
        TRANSPORT_USER_MISSING_CODE,
        TRANSPORT_USER_EMPTY_CODE,
        TRANSPORT_USER_OVERSIZED,
        TRANSPORT_DEVICE_SEND_FAILURE,
        TRANSPORT_DEVICE_STATUS_FAILURE,
        TRANSPORT_DEVICE_INVALID_JSON,
        TRANSPORT_DEVICE_MISSING_CODE,
        TRANSPORT_DEVICE_EMPTY_CODE,
        TRANSPORT_DEVICE_MISSING_VERIFIER,
        TRANSPORT_DEVICE_EMPTY_VERIFIER,
        TRANSPORT_EXCHANGE_SEND_FAILURE,
        TRANSPORT_EXCHANGE_STATUS_FAILURE,
        TRANSPORT_TOKEN_INVALID_JSON,
        TRANSPORT_TOKEN_MISSING_ACCESS,
        TRANSPORT_TOKEN_EMPTY_ACCESS,
        TRANSPORT_TOKEN_MISSING_REFRESH,
        TRANSPORT_TOKEN_BAD_EXPIRES,
        TRANSPORT_TOKEN_OVERFLOW_EXPIRES,
        TRANSPORT_TOKEN_NO_DOTS,
        TRANSPORT_TOKEN_ONE_DOT,
        TRANSPORT_TOKEN_EMPTY_PAYLOAD,
        TRANSPORT_TOKEN_BAD_BASE64,
        TRANSPORT_TOKEN_DASH_PAYLOAD,
        TRANSPORT_TOKEN_UNDERSCORE_PAYLOAD,
        TRANSPORT_TOKEN_PLUS_PAYLOAD,
        TRANSPORT_TOKEN_SLASH_PAYLOAD,
        TRANSPORT_TOKEN_NON_JSON_PAYLOAD,
        TRANSPORT_TOKEN_EMPTY_JSON_PAYLOAD,
    };
    struct telos_authentication *authentication;
    struct telos_authentication_status status;
    struct telos_cancel *cancel;
    struct telos_error *error = NULL;
    struct event_fixture events = {
        .mode = EVENT_ACCEPT,
    };

    assert(mkdtemp(directory) != NULL);
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);

    assert(!definition->login(NULL, NULL, capture_event, &events, &error));
    release_error(&error);
    assert(!definition->login(authentication, NULL, NULL, NULL, &error));
    release_error(&error);
    assert(!definition->status(NULL, &status, &error));
    release_error(&error);
    assert(!definition->status(authentication, NULL, &error));
    release_error(&error);
    assert(definition->resolve(NULL, "provider.openai", &error) == NULL);
    release_error(&error);
    assert(definition->resolve(authentication, NULL, &error) == NULL);
    release_error(&error);
    assert(definition->resolve(authentication, "provider.openai", &error) ==
           NULL);
    release_error(&error);
    assert(!definition->logout(NULL, &error));
    release_error(&error);

    for (size_t index = 0; index < sizeof(failures) / sizeof(failures[0]);
         ++index) {
        expect_login_failure(definition, authentication, &transport,
                             failures[index], EVENT_ACCEPT, NULL,
                             TELOS_AUTHENTICATION_SIGNED_OUT);
    }
    expect_login_failure(definition, authentication, &transport,
                         TRANSPORT_NORMAL, EVENT_REJECT_VERIFICATION, NULL,
                         TELOS_AUTHENTICATION_SIGNED_OUT);

    cancel = telos_cancel_create();
    assert(cancel != NULL);
    assert(telos_cancel_request(cancel));
    expect_login_failure(definition, authentication, &transport,
                         TRANSPORT_NORMAL, EVENT_ACCEPT, cancel,
                         TELOS_AUTHENTICATION_SIGNED_OUT);
    telos_cancel_release(cancel);

    cancel = telos_cancel_create();
    assert(cancel != NULL);
    expect_login_failure(definition, authentication, &transport,
                         TRANSPORT_DEVICE_PENDING_CANCEL, EVENT_ACCEPT, cancel,
                         TELOS_AUTHENTICATION_SIGNED_OUT);
    telos_cancel_release(cancel);

    expect_login_failure(definition, authentication, &transport,
                         TRANSPORT_NORMAL, EVENT_REJECT_COMPLETION, NULL,
                         TELOS_AUTHENTICATION_SIGNED_IN);
    assert(definition->logout(authentication, &error));
    assert(error == NULL);

    transport.mode = TRANSPORT_DEVICE_PENDING_ONCE;
    transport.cancel = NULL;
    transport.device_requests = 0;
    events = (struct event_fixture){
        .mode = EVENT_ACCEPT,
    };
    assert(definition->login(authentication, NULL, capture_event, &events,
                             &error));
    assert(error == NULL);
    assert(transport.device_requests == 2);
    assert(definition->logout(authentication, &error));
    assert(error == NULL);

    definition->destroy(authentication);
    assert(rmdir(directory) == 0);
}

static void
test_refresh_and_logout_failures(const authentication_definition *definition)
{
    char directory[] = "/tmp/telos-auth-invalid-refresh-XXXXXX";
    char cache_path[TEST_PATH_SIZE];
    char document[2048];
    struct transport_fixture transport = {
        .mode = TRANSPORT_REFRESH_STATUS_FAILURE,
    };
    const struct telos_authentication_config config = {
        .state_directory = directory,
        .service_endpoint = TELOS_OPENAI_CODEX_AUTH_ENDPOINT,
        .send = fake_send,
        .transport_context = &transport,
    };
    struct telos_authentication *authentication;
    struct telos_error *error = NULL;
    char *secret;

    assert(mkdtemp(directory) != NULL);
    assert(snprintf(cache_path, sizeof(cache_path), "%s/%s", directory,
                    "openai-codex-auth.json") < (int)sizeof(cache_path));
    assert(snprintf(document, sizeof(document),
                    "{\"access_token\":\"%s\","
                    "\"refresh_token\":\"refresh-failure\","
                    "\"account_id\":\"acct-failure\",\"expires_at\":1}",
                    access_token) < (int)sizeof(document));
    write_cache(cache_path, document, strlen(document), 0600);
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);

    assert(definition->resolve(authentication, "provider.openai", &error) ==
           NULL);
    release_error(&error);
    transport.mode = TRANSPORT_REFRESH_SEND_FAILURE;
    assert(definition->resolve(authentication, "provider.openai", &error) ==
           NULL);
    release_error(&error);
    transport.mode = TRANSPORT_REFRESH_INVALID_JSON;
    assert(definition->resolve(authentication, "provider.openai", &error) ==
           NULL);
    release_error(&error);
    transport.mode = TRANSPORT_REFRESH_WITHOUT_REPLACEMENT;
    secret = definition->resolve(authentication, "provider.openai", &error);
    assert(secret != NULL);
    assert(error == NULL);
    assert(strcmp(secret, access_token) == 0);
    memset(secret, 0, strlen(secret));
    free(secret);
    assert(definition->logout(authentication, &error));
    assert(error == NULL);

    assert(mkdir(cache_path, 0700) == 0);
    assert(!definition->logout(authentication, &error));
    release_error(&error);
    assert(rmdir(cache_path) == 0);
    definition->destroy(authentication);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    const struct telos_authentication_definition_v1 *definition =
        telos_openai_codex_authentication_definition();

    assert(definition != NULL);
    test_create_and_cache_failures(definition);
    test_login_failures(definition);
    test_refresh_and_logout_failures(definition);
    definition->destroy(NULL);
    return 0;
}
