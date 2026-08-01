#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <telos/plugin.h>

static void log_message(void *context, int level, const char *message)
{
    unsigned int *calls = context;

    (void)level;
    (void)message;
    *calls += 1;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

int main(void)
{
    struct telos_plugin_instance *plugin =
        telos_plugin_instance_create("dev.zevorn.echo", NULL);
    const enum telos_plugin_state states[] = {
        TELOS_PLUGIN_VERIFIED,
        TELOS_PLUGIN_LOADED,
        TELOS_PLUGIN_INITIALIZING,
    };
    struct telos_error *error = NULL;
    struct telos_host_api_v1 host;
    unsigned int log_calls = 0;

    assert(!telos_host_api_v1_initialize(NULL, NULL, log_message, &error));
    clear_error(&error);
    assert(!telos_host_api_v1_initialize(&host, NULL, NULL, &error));
    clear_error(&error);
    assert(
        telos_host_api_v1_initialize(&host, &log_calls, log_message, &error));
    assert(error == NULL);
    assert(host.abi_version == TELOS_PLUGIN_ABI_VERSION);
    assert(host.struct_size == sizeof(host));
    assert(host.context == &log_calls);
    assert(host.allocator != NULL);
    assert(host.value != NULL);
    assert(host.event != NULL);
    assert(host.clock.now != NULL);

    assert(telos_plugin_instance_create(NULL, &error) == NULL);
    clear_error(&error);
    assert(telos_plugin_instance_create("", &error) == NULL);
    clear_error(&error);
    assert(telos_plugin_instance_retain(NULL) == NULL);
    telos_plugin_instance_release(NULL);
    assert(telos_plugin_instance_state(NULL) == 0);
    assert(!telos_plugin_instance_set_healthy(NULL, true, &error));
    clear_error(&error);
    assert(
        !telos_plugin_instance_transition(NULL, TELOS_PLUGIN_VERIFIED, &error));
    clear_error(&error);
    assert(!telos_plugin_instance_set_healthy(plugin, true, &error));
    assert(telos_error_code(error) == EINVAL);
    clear_error(&error);
    assert(
        !telos_plugin_instance_transition(plugin, TELOS_PLUGIN_ACTIVE, &error));
    clear_error(&error);

    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]);
         ++index) {
        if (!telos_plugin_instance_transition(plugin, states[index], NULL)) {
            fputs("Plugin lifecycle rejected an ordered transition\n", stderr);
            telos_plugin_instance_release(plugin);
            return 1;
        }
    }

    if (telos_plugin_instance_transition(plugin, TELOS_PLUGIN_ACTIVE, &error) ||
        error == NULL) {
        fputs("unhealthy Plugin became active\n", stderr);
        telos_error_release(error);
        telos_plugin_instance_release(plugin);
        return 1;
    }
    telos_error_release(error);
    error = NULL;

    if (!telos_plugin_instance_set_healthy(plugin, true, NULL) ||
        !telos_plugin_instance_transition(plugin, TELOS_PLUGIN_ACTIVE, NULL) ||
        !telos_plugin_instance_transition(plugin, TELOS_PLUGIN_QUIESCING,
                                          NULL)) {
        fputs("healthy Plugin did not activate and quiesce\n", stderr);
        telos_plugin_instance_release(plugin);
        return 1;
    }

    {
        struct telos_plugin_instance *session_pin =
            telos_plugin_instance_retain(plugin);

        if (telos_plugin_instance_transition(plugin, TELOS_PLUGIN_STOPPED,
                                             &error) ||
            error == NULL) {
            fputs("Plugin stopped while a Session still pinned it\n", stderr);
            telos_error_release(error);
            telos_plugin_instance_release(session_pin);
            telos_plugin_instance_release(plugin);
            return 1;
        }
        telos_error_release(error);
        error = NULL;
        telos_plugin_instance_release(session_pin);
    }

    if (!telos_plugin_instance_transition(plugin, TELOS_PLUGIN_STOPPED, NULL) ||
        !telos_plugin_instance_transition(plugin, TELOS_PLUGIN_UNLOADED,
                                          NULL) ||
        telos_plugin_instance_state(plugin) != TELOS_PLUGIN_UNLOADED) {
        fputs("Plugin lifecycle did not drain and unload\n", stderr);
        telos_plugin_instance_release(plugin);
        return 1;
    }

    telos_plugin_instance_release(plugin);
    return 0;
}
