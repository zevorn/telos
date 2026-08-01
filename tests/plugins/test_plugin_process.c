#include <assert.h>
#include <errno.h>
#include <string.h>

#include <telos/plugin_process.h>

int main(int argc, char **argv)
{
    struct telos_error *error = NULL;
    struct telos_plugin_process *process;
    struct telos_value *body;
    struct telos_value *response = NULL;
    struct telos_cancel *cancel;
    char long_type[242];

    assert(argc == 3);
    assert(telos_plugin_process_spawn(NULL, &error) == NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_plugin_process_spawn("", &error) == NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_plugin_process_spawn_plugin(argv[1], NULL, "plugin", &error) ==
           NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_plugin_process_spawn_plugin(argv[1], argv[2], NULL, &error) ==
           NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_plugin_process_spawn_plugin(argv[1], "", "plugin", &error) ==
           NULL);
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_shutdown(NULL, 10, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    telos_plugin_process_destroy(NULL);

    process = telos_plugin_process_spawn_plugin(
        argv[1], argv[2], "dev.zevorn.process-fixture", &error);
    assert(process != NULL);
    assert(error == NULL);
    body = telos_value_new_null();
    assert(telos_plugin_process_request(process, "health", body, 1000, NULL,
                                        &response, &error));
    assert(strcmp(telos_value_string(response), "healthy") == 0);
    telos_value_release(response);
    response = NULL;
    telos_value_release(body);
    body = telos_value_new_string("through loaded plugin");
    assert(telos_plugin_process_execute_tool(process, "dev.zevorn.process-echo",
                                             body, 1000, NULL, &response,
                                             &error));
    assert(strcmp(telos_value_string(response), "through loaded plugin") == 0);
    telos_value_release(response);
    response = NULL;
    assert(!telos_plugin_process_execute_tool(process, "missing.tool", body,
                                              1000, NULL, &response, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    assert(telos_plugin_process_shutdown(process, 1000, &error));
    telos_value_release(body);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(process, "health", body, 1000, NULL,
                                         &response, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    telos_value_release(body);
    telos_plugin_process_destroy(process);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(NULL, "health", body, 1000, NULL,
                                         &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_request(process, NULL, body, 1000, NULL,
                                         &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_request(process, "", body, 1000, NULL,
                                         &response, &error));
    telos_error_release(error);
    error = NULL;
    memset(long_type, 'x', sizeof(long_type) - 1);
    long_type[sizeof(long_type) - 1] = '\0';
    assert(!telos_plugin_process_request(process, long_type, body, 1000, NULL,
                                         &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_request(process, "health", NULL, 1000, NULL,
                                         &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_request(process, "health", body, 1000, NULL,
                                         NULL, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_execute_tool(process, NULL, body, 1000, NULL,
                                              &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_execute_tool(process, "", body, 1000, NULL,
                                              &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_execute_tool(process, "tool", NULL, 1000, NULL,
                                              &response, &error));
    telos_error_release(error);
    error = NULL;
    assert(!telos_plugin_process_request(process, "crash", body, 1000, NULL,
                                         &response, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PLUGIN);
    telos_error_release(error);
    error = NULL;
    telos_plugin_process_destroy(process);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    cancel = telos_cancel_create();
    telos_cancel_request(cancel);
    assert(!telos_plugin_process_request(process, "hang", body, 1000, cancel,
                                         &response, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_CANCELLED);
    telos_error_release(error);
    error = NULL;
    telos_cancel_release(cancel);
    telos_plugin_process_destroy(process);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    assert(!telos_plugin_process_request(process, "hang", body, 50, NULL,
                                         &response, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_TIMEOUT);
    assert(telos_error_code(error) == ETIMEDOUT);
    telos_error_release(error);
    telos_plugin_process_destroy(process);
    telos_value_release(body);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(process, "partial", body, 50, NULL,
                                         &response, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_TIMEOUT);
    telos_error_release(error);
    error = NULL;
    telos_plugin_process_destroy(process);
    telos_value_release(body);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(process, "not-authorized", body, 1000,
                                         NULL, &response, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PLUGIN);
    telos_error_release(error);
    telos_plugin_process_shutdown(process, 1000, NULL);
    telos_plugin_process_destroy(process);
    telos_value_release(body);

    {
        const char *invalid_responses[] = {
            "wrong-response", "missing-body", "error-nonstring",
            "malformed",      "oversized",
        };

        body = telos_value_new_null();
        for (size_t index = 0;
             index < sizeof(invalid_responses) / sizeof(invalid_responses[0]);
             ++index) {
            process = telos_plugin_process_spawn(argv[1], &error);
            assert(process != NULL);
            assert(!telos_plugin_process_request(
                process, invalid_responses[index], body, 1000, NULL, &response,
                &error));
            assert(error != NULL);
            telos_error_release(error);
            error = NULL;
            telos_plugin_process_destroy(process);
        }
        telos_value_release(body);
    }

    process = telos_plugin_process_spawn("/missing/telos-plugin-host", &error);
    assert(process != NULL);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(process, "health", body, 1000, NULL,
                                         &response, &error));
    assert(error != NULL);
    telos_error_release(error);
    telos_value_release(body);
    telos_plugin_process_destroy(process);
    return 0;
}
