#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/plugins/api_key_auth.h>

typedef struct telos_authentication_definition_v1 telos_auth_definition;

struct event_fixture {
    size_t completed;
};

static bool capture_event(const struct telos_authentication_event *event,
                          void *context,
                          struct telos_error **error)
{
    struct event_fixture *fixture = context;

    (void)error;
    assert(event != NULL);
    assert(event->kind == TELOS_AUTHENTICATION_COMPLETED);
    assert(event->verification_uri == NULL);
    assert(event->user_code == NULL);
    fixture->completed += 1;
    return true;
}

static void verify_profile(const telos_auth_definition *definition,
                           const char *environment, const char *target,
                           const char *key, const char *cache_name,
                           const char *state_directory)
{
    const struct telos_authentication_config config = {
        .state_directory = state_directory,
    };
    struct telos_authentication_status status;
    struct event_fixture events = {0};
    struct telos_error *error = NULL;
    struct telos_authentication *authentication;
    char path[4096];
    struct stat file_status;
    char *secret;

    unsetenv(environment);
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(error == NULL);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_OUT);
    assert(!definition->login(authentication, NULL, capture_event, &events,
                              &error));
    assert(error != NULL);
    telos_error_release(error);
    definition->destroy(authentication);

    assert(setenv(environment, key, 1) == 0);
    authentication = definition->create(&config, &error);
    assert(authentication != NULL);
    assert(definition->login(authentication, NULL, capture_event, &events,
                             &error));
    assert(events.completed == 1);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_IN);
    assert(strcmp(status.provider, target + sizeof("provider.") - 1) == 0);
    secret = definition->resolve(authentication, target, &error);
    assert(secret != NULL);
    assert(strcmp(secret, key) == 0);
    free(secret);
    secret = definition->resolve(authentication, "provider.invalid", &error);
    assert(secret == NULL);
    telos_error_release(error);
    error = NULL;
    assert(snprintf(path, sizeof(path), "%s/%s.key", state_directory,
                    cache_name) < (int)sizeof(path));
    assert(stat(path, &file_status) == 0);
    assert((file_status.st_mode & 0077) == 0);
    assert(definition->logout(authentication, &error));
    assert(stat(path, &file_status) != 0);
    assert(definition->status(authentication, &status, &error));
    assert(status.state == TELOS_AUTHENTICATION_SIGNED_OUT);
    definition->destroy(authentication);
    unsetenv(environment);
}

int main(void)
{
    const char state_template[] = "/tmp/telos-api-key-auth-XXXXXX";
    const struct telos_authentication_definition_v1 *deepseek =
        telos_deepseek_api_key_authentication_definition();
    const struct telos_authentication_definition_v1 *zai =
        telos_zai_api_key_authentication_definition();
    const struct telos_authentication_definition_v1 *anthropic =
        telos_anthropic_api_key_authentication_definition();
    char state_directory[sizeof(state_template)];

    memcpy(state_directory, state_template, sizeof(state_template));
    assert(mkdtemp(state_directory) != NULL);
    verify_profile(deepseek, "DEEPSEEK_API_KEY", "provider.deepseek",
                   "deepseek-test-key", "deepseek-api-key", state_directory);
    verify_profile(zai, "ZAI_API_KEY", "provider.zai", "zai-test-key",
                   "zai-api-key", state_directory);
    verify_profile(anthropic, "ANTHROPIC_API_KEY", "provider.anthropic",
                   "anthropic-test-key", "anthropic-api-key", state_directory);
    assert(rmdir(state_directory) == 0);
    return 0;
}
