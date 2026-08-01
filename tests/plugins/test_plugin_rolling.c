#include <assert.h>

#include <telos/plugin.h>

static void activate(struct telos_plugin_instance *plugin)
{
    assert(
        telos_plugin_instance_transition(plugin, TELOS_PLUGIN_VERIFIED, NULL));
    assert(telos_plugin_instance_transition(plugin, TELOS_PLUGIN_LOADED, NULL));
    assert(telos_plugin_instance_transition(plugin, TELOS_PLUGIN_INITIALIZING,
                                            NULL));
    assert(telos_plugin_instance_set_healthy(plugin, true, NULL));
    assert(telos_plugin_instance_transition(plugin, TELOS_PLUGIN_ACTIVE, NULL));
}

static void publish(struct telos_registry *registry, const void *implementation)
{
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "dev.zevorn.rolling", NULL);
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.rolling.echo",
        .plugin_id = "dev.zevorn.rolling",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = implementation,
    };

    assert(telos_registry_transaction_add(transaction, &descriptor, NULL));
    assert(telos_registry_transaction_commit(transaction, NULL));
}

int main(void)
{
    int implementation_v1 = 1;
    int implementation_v2 = 2;
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    struct telos_plugin_instance *version_one =
        telos_plugin_instance_create("dev.zevorn.rolling@1", NULL);
    struct telos_plugin_instance *version_two =
        telos_plugin_instance_create("dev.zevorn.rolling@2", NULL);
    struct telos_registry_generation *old_generation;
    struct telos_registry_generation *new_generation;
    struct telos_plugin_instance *old_session_pin;
    struct telos_error *error = NULL;

    activate(version_one);
    publish(registry, &implementation_v1);
    old_generation = telos_registry_acquire(registry);
    old_session_pin = telos_plugin_instance_retain(version_one);

    activate(version_two);
    publish(registry, &implementation_v2);
    new_generation = telos_registry_acquire(registry);
    assert(telos_registry_generation_find(old_generation, TELOS_EXTENSION_TOOL,
                                          "dev.zevorn.rolling.echo")
               ->implementation == &implementation_v1);
    assert(telos_registry_generation_find(new_generation, TELOS_EXTENSION_TOOL,
                                          "dev.zevorn.rolling.echo")
               ->implementation == &implementation_v2);
    assert(telos_plugin_instance_transition(version_one, TELOS_PLUGIN_QUIESCING,
                                            NULL));
    assert(!telos_plugin_instance_transition(version_one, TELOS_PLUGIN_STOPPED,
                                             &error));
    assert(error != NULL);
    telos_error_release(error);
    telos_plugin_instance_release(old_session_pin);
    assert(telos_plugin_instance_transition(version_one, TELOS_PLUGIN_STOPPED,
                                            NULL));
    assert(telos_plugin_instance_transition(version_one, TELOS_PLUGIN_UNLOADED,
                                            NULL));

    telos_registry_generation_release(new_generation);
    telos_registry_generation_release(old_generation);
    telos_plugin_instance_release(version_two);
    telos_plugin_instance_release(version_one);
    telos_registry_destroy(registry);
    return 0;
}
