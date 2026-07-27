#include <stdio.h>

#include <telos/plugin.h>

int main(void)
{
    struct telos_plugin_instance *plugin = telos_plugin_instance_create(
        "dev.zevorn.echo",
        NULL
    );
    const enum telos_plugin_state states[] = {
        TELOS_PLUGIN_VERIFIED,
        TELOS_PLUGIN_LOADED,
        TELOS_PLUGIN_INITIALIZING,
    };
    struct telos_error *error = NULL;

    for (size_t index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        if (!telos_plugin_instance_transition(plugin, states[index], NULL)) {
            fputs("Plugin lifecycle rejected an ordered transition\n", stderr);
            telos_plugin_instance_release(plugin);
            return 1;
        }
    }

    if (
        telos_plugin_instance_transition(
            plugin,
            TELOS_PLUGIN_ACTIVE,
            &error
        )
        || error == NULL
    ) {
        fputs("unhealthy Plugin became active\n", stderr);
        telos_error_release(error);
        telos_plugin_instance_release(plugin);
        return 1;
    }
    telos_error_release(error);
    error = NULL;

    if (
        !telos_plugin_instance_set_healthy(plugin, true, NULL)
        || !telos_plugin_instance_transition(
            plugin,
            TELOS_PLUGIN_ACTIVE,
            NULL
        )
        || !telos_plugin_instance_transition(
            plugin,
            TELOS_PLUGIN_QUIESCING,
            NULL
        )
    ) {
        fputs("healthy Plugin did not activate and quiesce\n", stderr);
        telos_plugin_instance_release(plugin);
        return 1;
    }

    {
        struct telos_plugin_instance *session_pin =
            telos_plugin_instance_retain(plugin);

        if (
            telos_plugin_instance_transition(
                plugin,
                TELOS_PLUGIN_STOPPED,
                &error
            )
            || error == NULL
        ) {
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

    if (
        !telos_plugin_instance_transition(
            plugin,
            TELOS_PLUGIN_STOPPED,
            NULL
        )
        || !telos_plugin_instance_transition(
            plugin,
            TELOS_PLUGIN_UNLOADED,
            NULL
        )
        || telos_plugin_instance_state(plugin) != TELOS_PLUGIN_UNLOADED
    ) {
        fputs("Plugin lifecycle did not drain and unload\n", stderr);
        telos_plugin_instance_release(plugin);
        return 1;
    }

    telos_plugin_instance_release(plugin);
    return 0;
}
