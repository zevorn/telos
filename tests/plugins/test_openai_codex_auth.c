#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/authentication.h>
#include <telos/plugins/openai_codex_auth.h>

static const char access_token[] =
    "e30."
    "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50"
    "X2lkIjoiYWNjdC10ZXN0In19.signature";

struct transport_fixture {
    size_t user_code_requests;
    size_t device_token_requests;
    size_t exchange_requests;
    size_t refresh_requests;
};

struct event_fixture {
    size_t verification_events;
    size_t completed_events;
    char verification_uri[128];
    char user_code[32];
};

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
    assert(request->bearer_token == NULL);
    if (strstr(request->url, "/api/accounts/deviceauth/usercode") != NULL) {
        fixture->user_code_requests += 1;
        assert(strcmp(request->content_type, "application/json") == 0);
        assert(strstr(request->body,
                      "\"client_id\":\"app_EMoamEEZ73f0CkXaXp7hrann\"") !=
               NULL);
        return send_json(receive, receive_context, status_code, 200,
                         "{\"device_auth_id\":\"device-test\","
                         "\"user_code\":\"ABCD-1234\",\"interval\":0}",
                         error);
    }
    if (strstr(request->url, "/api/accounts/deviceauth/token") != NULL) {
        fixture->device_token_requests += 1;
        assert(strstr(request->body,
                      "\"device_auth_id\":\"device-test\"") != NULL);
        assert(strstr(request->body, "\"user_code\":\"ABCD-1234\"") !=
               NULL);
        if (fixture->device_token_requests == 1) {
            return send_json(receive, receive_context, status_code, 403,
                             "{\"error\":"
                             "\"deviceauth_authorization_pending\"}",
                             error);
        }
        return send_json(receive, receive_context, status_code, 200,
                         "{\"authorization_code\":\"authorization-test\","
                         "\"code_verifier\":\"verifier-test\"}", error);
    }
    if (strstr(request->url, "/oauth/token") != NULL) {
        char response[1024];

        assert(strcmp(request->content_type,
                      "application/x-www-form-urlencoded") == 0);
        if (strstr(request->body, "grant_type=refresh_token") != NULL) {
            fixture->refresh_requests += 1;
            assert(strstr(request->body,
                          "refresh_token=refresh-test") != NULL);
            assert(snprintf(response, sizeof(response),
                            "{\"access_token\":\"%s\","
                            "\"refresh_token\":\"refresh-next\","
                            "\"expires_in\":3600}", access_token) <
                   (int)sizeof(response));
            return send_json(receive, receive_context, status_code, 200,
                             response, error);
        }
        fixture->exchange_requests += 1;
        assert(strstr(request->body,
                      "grant_type=authorization_code") != NULL);
        assert(strstr(request->body,
                      "code=authorization-test") != NULL);
        assert(strstr(request->body,
                      "code_verifier=verifier-test") != NULL);
        assert(snprintf(response, sizeof(response),
                        "{\"access_token\":\"%s\","
                        "\"refresh_token\":\"refresh-test\","
                        "\"expires_in\":1}", access_token) <
               (int)sizeof(response));
        return send_json(receive, receive_context, status_code, 200,
                         response, error);
    }
    abort();
}

static bool capture_event(const struct telos_authentication_event *event,
                          void *context,
                          struct telos_error **error)
{
    struct event_fixture *fixture = context;

    (void)error;
    assert(event != NULL);
    if (event->kind == TELOS_AUTHENTICATION_VERIFICATION_REQUIRED) {
        fixture->verification_events += 1;
        assert(event->verification_uri != NULL);
        assert(event->user_code != NULL);
        assert(strlen(event->verification_uri) <
               sizeof(fixture->verification_uri));
        assert(strlen(event->user_code) < sizeof(fixture->user_code));
        strcpy(fixture->verification_uri, event->verification_uri);
        strcpy(fixture->user_code, event->user_code);
    } else if (event->kind == TELOS_AUTHENTICATION_COMPLETED) {
        fixture->completed_events += 1;
    } else {
        abort();
    }
    return true;
}

int main(void)
{
    const struct telos_authentication_definition_v1 *definition =
        telos_openai_codex_authentication_definition();
    char state_directory[] = "/tmp/telos-openai-auth-XXXXXX";
    struct transport_fixture transport = {0};
    struct event_fixture events = {0};
    const struct telos_authentication_config config = {
        .state_directory = state_directory,
        .service_endpoint = "http://127.0.0.1:1234",
        .send = fake_send,
        .transport_context = &transport,
    };
    const struct telos_authentication_config invalid_endpoint = {
        .state_directory = state_directory,
        .service_endpoint = "https://auth.openai.com.example.invalid",
        .send = fake_send,
        .transport_context = &transport,
    };
    char cache_path[4096];
    struct stat cache_status;
    struct telos_authentication_status status;
    struct telos_authentication *authentication;
    struct telos_error *error = NULL;
    char *secret;

    assert(mkdtemp(state_directory) != NULL);
    assert(definition != NULL);
    assert(definition->struct_size >= sizeof(*definition));
    assert(strcmp(definition->id, "dev.zevorn.openai-codex-auth") == 0);
    assert(definition->create != NULL);
    assert(definition->destroy != NULL);
    assert(definition->login != NULL);
    assert(definition->logout != NULL);
    assert(definition->status != NULL);
    assert(definition->resolve != NULL);

    assert(definition->create(&invalid_endpoint, &error) == NULL);
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    telos_error_release(error);
    error = NULL;

    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    assert(definition->status(authentication, &status, &error));
    assert(error == NULL);
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_OUT);
    assert(status.account_id == NULL);

    assert(definition->login(authentication, NULL, capture_event, &events,
                             &error));
    assert(error == NULL);
    assert(transport.user_code_requests == 1);
    assert(transport.device_token_requests == 2);
    assert(transport.exchange_requests == 1);
    assert(transport.refresh_requests == 0);
    assert(events.verification_events == 1);
    assert(events.completed_events == 1);
    assert(strcmp(events.verification_uri,
                  "http://127.0.0.1:1234/codex/device") == 0);
    assert(strcmp(events.user_code, "ABCD-1234") == 0);
    assert(snprintf(cache_path, sizeof(cache_path), "%s/%s",
                    state_directory, "openai-codex-auth.json") <
           (int)sizeof(cache_path));
    assert(stat(cache_path, &cache_status) == 0);
    assert((cache_status.st_mode & 0777) == 0600);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_IN);
    assert(strcmp(status.account_id, "acct-test") == 0);

    secret = definition->resolve(authentication, "provider.openai", &error);
    assert(secret != NULL);
    assert(error == NULL);
    assert(strcmp(secret, access_token) == 0);
    assert(transport.refresh_requests == 1);
    memset(secret, 0, strlen(secret));
    free(secret);
    definition->destroy(authentication);

    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_IN);
    assert(strcmp(status.account_id, "acct-test") == 0);
    assert(definition->resolve(authentication, "provider.other", &error) ==
           NULL);
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION);
    telos_error_release(error);
    error = NULL;

    assert(definition->logout(authentication, &error));
    assert(error == NULL);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_OUT);
    definition->destroy(authentication);
    assert(rmdir(state_directory) == 0);
    return 0;
}
