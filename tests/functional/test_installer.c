#define _XOPEN_SOURCE 700

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <telos/install.h>

struct observations {
    enum telos_install_state states[128];
    size_t state_count;
    size_t approvals;
    bool allow;
    bool native_build;
};

static bool approve(const struct telos_install_risk *risk, void *context)
{
    struct observations *observations = context;

    assert(risk->requires_approval);
    observations->native_build = risk->native_build;
    observations->approvals += 1;
    return observations->allow;
}

static void record_progress(enum telos_install_state state, void *context)
{
    struct observations *observations = context;

    assert(observations->state_count < 128);
    observations->states[observations->state_count++] = state;
}

static bool saw_state(const struct observations *observations,
                      enum telos_install_state state)
{
    for (size_t index = 0; index < observations->state_count; ++index) {
        if (observations->states[index] == state) {
            return true;
        }
    }
    return false;
}

static struct telos_install_options options(const char *source,
                                            const char *state,
                                            const char *pkgconfig,
                                            const char *sysroot,
                                            const char *abi_check,
                                            const char *plugin_host,
                                            struct observations *observations)
{
    return (struct telos_install_options){
        .source = source,
        .state_directory = state,
        .sdk_pkgconfig_path = pkgconfig,
        .sdk_sysroot = sysroot,
        .abi_check_path = abi_check,
        .plugin_host_path = plugin_host,
        .builder = TELOS_BUILDER_NATIVE,
        .timeout_seconds = 60,
        .approve = approve,
        .approve_context = observations,
        .progress = record_progress,
        .progress_context = observations,
    };
}

static void assert_preserved(struct telos_install_options *install_options,
                             struct observations *observations,
                             const char *source,
                             enum telos_install_state expected_state,
                             enum telos_error_domain expected_domain,
                             const char *current_path,
                             const char *expected_target)
{
    struct telos_install_result result = {0};
    struct telos_error *error = NULL;
    char actual_target[4096];
    ssize_t actual_size;

    install_options->source = source;
    assert(!telos_plugin_install(install_options, NULL, &result, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == expected_domain);
    assert(observations->states[observations->state_count - 1] ==
           expected_state);
    actual_size =
        readlink(current_path, actual_target, sizeof(actual_target) - 1);
    assert(actual_size > 0);
    actual_target[actual_size] = '\0';
    assert(strcmp(expected_target, actual_target) == 0);
    telos_error_release(error);
    telos_install_result_clear(&result);
}

struct cancel_request {
    struct telos_cancel *cancel;
};

static void *request_cancel(void *context)
{
    struct cancel_request *request = context;
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 50000000,
    };

    nanosleep(&delay, NULL);
    assert(telos_cancel_request(request->cancel));
    return NULL;
}

int main(int argc, char **argv)
{
    char git_source[4096];
    struct observations first_observations = {.allow = true};
    struct observations second_observations = {.allow = true};
    struct observations denied_observations = {.allow = false};
    struct observations container_observations = {.allow = true};
    struct telos_install_options first;
    struct telos_install_options second;
    struct telos_install_options denied;
    struct telos_install_options container;
    struct telos_install_result first_result = {0};
    struct telos_install_result second_result = {0};
    struct telos_install_result denied_result = {0};
    struct telos_install_result container_result = {0};
    struct telos_error *error = NULL;
    struct telos_cancel *cancel;
    char current_path[4096];
    char before[4096];
    char after[4096];
    ssize_t before_size;
    ssize_t after_size;

    assert(argc == 14);
    assert(snprintf(git_source, sizeof(git_source), "git:%s", argv[2]) <
           (int)sizeof(git_source));
    first = options(argv[1], argv[3], argv[4], argv[5], argv[6], argv[7],
                    &first_observations);
    second = options(git_source, argv[3], argv[4], argv[5], argv[6], argv[7],
                     &second_observations);
    denied = options(argv[1], argv[3], argv[4], argv[5], argv[6], argv[7],
                     &denied_observations);
    container = options(argv[8], argv[3], argv[4], argv[5], argv[6], argv[7],
                        &container_observations);
    container.builder = TELOS_BUILDER_CONTAINER;
    container.container_image = "telos-test-builder";

    if (!telos_plugin_install(&first, NULL, &first_result, &error)) {
        fprintf(
            stderr, "first install failed at state %d: %s\n",
            first_observations.state_count == 0
                ? 0
                : first_observations.states[first_observations.state_count - 1],
            telos_error_message(error));
        return 1;
    }
    assert(error == NULL);
    assert(strcmp(first_result.plugin_id, "dev.example.echo-tool") == 0);
    assert(!first_result.cache_hit);
    assert(access(first_result.artifact_path, R_OK) == 0);
    assert(first_observations.approvals == 1);
    assert(first_observations.native_build);
    assert(saw_state(&first_observations, TELOS_INSTALL_BUILD));
    assert(saw_state(&first_observations, TELOS_INSTALL_TEST));
    assert(saw_state(&first_observations, TELOS_INSTALL_ABI_CHECK));
    assert(first_observations.states[first_observations.state_count - 1] ==
           TELOS_INSTALL_COMPLETED);

    assert(telos_plugin_install(&second, NULL, &second_result, &error));
    assert(error == NULL);
    assert(second_result.cache_hit);
    assert(strcmp(first_result.cache_key, second_result.cache_key) == 0);
    assert(second_observations.approvals == 1);
    assert(!saw_state(&second_observations, TELOS_INSTALL_BUILD));
    assert(telos_plugin_rollback(argv[3], "dev.example.echo-tool", &error));
    assert(error == NULL);
    assert(telos_plugin_install(&container, NULL, &container_result, &error));
    assert(error == NULL);
    assert(!container_result.cache_hit);
    assert(container_observations.approvals == 1);
    assert(!container_observations.native_build);
    assert(saw_state(&container_observations, TELOS_INSTALL_BUILD));
    assert(saw_state(&container_observations, TELOS_INSTALL_TEST));
    assert(saw_state(&container_observations, TELOS_INSTALL_STAGE));
    assert(saw_state(&container_observations, TELOS_INSTALL_ABI_CHECK));
    assert(saw_state(&container_observations, TELOS_INSTALL_HEALTH_CHECK));

    assert(snprintf(current_path, sizeof(current_path),
                    "%s/plugins/dev.example.echo-tool/current",
                    argv[3]) < (int)sizeof(current_path));
    before_size = readlink(current_path, before, sizeof(before) - 1);
    assert(before_size > 0);
    before[before_size] = '\0';
    assert_preserved(
        &first, &first_observations, argv[9], TELOS_INSTALL_BUILD,
        TELOS_ERROR_DOMAIN_PLUGIN, current_path, before);
    assert_preserved(
        &first, &first_observations, argv[10], TELOS_INSTALL_TEST,
        TELOS_ERROR_DOMAIN_PLUGIN, current_path, before);
    assert_preserved(
        &first, &first_observations, argv[11], TELOS_INSTALL_ABI_CHECK,
        TELOS_ERROR_DOMAIN_PLUGIN, current_path, before);
    assert_preserved(
        &first, &first_observations, argv[12], TELOS_INSTALL_HEALTH_CHECK,
        TELOS_ERROR_DOMAIN_PLUGIN, current_path, before);

    first.timeout_seconds = 1;
    assert_preserved(
        &first, &first_observations, argv[13], TELOS_INSTALL_BUILD,
        TELOS_ERROR_DOMAIN_TIMEOUT, current_path, before);
    first.timeout_seconds = 60;

    cancel = telos_cancel_create();
    {
        pthread_t thread;
        struct cancel_request request = {.cancel = cancel};

        assert(pthread_create(&thread, NULL, request_cancel, &request) == 0);
        first.source = argv[13];
        assert(!telos_plugin_install(&first, cancel, &denied_result, &error));
        assert(pthread_join(thread, NULL) == 0);
    }
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_CANCELLED);
    telos_error_release(error);
    error = NULL;
    telos_cancel_release(cancel);

    after_size = readlink(current_path, after, sizeof(after) - 1);
    assert(after_size > 0);
    after[after_size] = '\0';
    assert(strcmp(before, after) == 0);

    assert(!telos_plugin_install(&denied, NULL, &denied_result, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION);
    assert(telos_error_code(error) == EPERM);
    assert(denied_observations.approvals == 1);
    telos_error_release(error);

    telos_install_result_clear(&denied_result);
    telos_install_result_clear(&container_result);
    telos_install_result_clear(&second_result);
    telos_install_result_clear(&first_result);
    return 0;
}
