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

    assert(argc == 2);
    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    assert(error == NULL);
    body = telos_value_new_null();
    assert(telos_plugin_process_request(
        process,
        "health",
        body,
        1000,
        NULL,
        &response,
        &error
    ));
    assert(strcmp(telos_value_string(response), "healthy") == 0);
    telos_value_release(response);
    assert(telos_plugin_process_shutdown(process, 1000, &error));
    telos_plugin_process_destroy(process);
    telos_value_release(body);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    body = telos_value_new_null();
    assert(!telos_plugin_process_request(
        process,
        "crash",
        body,
        1000,
        NULL,
        &response,
        &error
    ));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PLUGIN);
    telos_error_release(error);
    error = NULL;
    telos_plugin_process_destroy(process);

    process = telos_plugin_process_spawn(argv[1], &error);
    assert(process != NULL);
    assert(!telos_plugin_process_request(
        process,
        "hang",
        body,
        50,
        NULL,
        &response,
        &error
    ));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_TIMEOUT);
    assert(telos_error_code(error) == ETIMEDOUT);
    telos_error_release(error);
    telos_plugin_process_destroy(process);
    telos_value_release(body);
    return 0;
}
