#include <stdio.h>

#include <telos/plugin.h>

int main(int argc, char **argv)
{
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    const struct telos_host_api_v1 incompatible = {
        .abi_version = TELOS_PLUGIN_ABI_VERSION + 1,
        .struct_size = sizeof(incompatible),
    };
    struct telos_error *error = NULL;
    struct telos_plugin_module *module;
    struct telos_registry_generation *generation;

    if (argc != 2) {
        return 1;
    }
    module = telos_plugin_module_load_inprocess(
        argv[1],
        "fixture.bundle",
        &incompatible,
        registry,
        &error
    );
    generation = telos_registry_acquire(registry);
    if (
        module != NULL
        || error == NULL
        || telos_error_domain(error) != TELOS_ERROR_DOMAIN_PLUGIN
        || telos_registry_generation_count(generation) != 0
    ) {
        fputs("incompatible Plugin ABI changed the Registry\n", stderr);
        telos_registry_generation_release(generation);
        telos_error_release(error);
        telos_plugin_module_destroy(module);
        telos_registry_destroy(registry);
        return 1;
    }

    telos_registry_generation_release(generation);
    telos_error_release(error);
    telos_registry_destroy(registry);
    return 0;
}
