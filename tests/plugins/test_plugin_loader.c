#include <stdio.h>

#include <telos/plugin.h>

int main(int argc, char **argv)
{
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    const struct telos_host_api_v1 host = {
        .abi_version = TELOS_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(host),
    };
    struct telos_plugin_module *module;
    struct telos_registry_generation *generation;
    bool passed;

    if (argc != 2) {
        return 1;
    }
    module = telos_plugin_module_load_inprocess(argv[1], "fixture.bundle",
                                                &host, registry, NULL);
    generation = telos_registry_acquire(registry);
    passed =
        module != NULL && generation != NULL &&
        telos_plugin_instance_state(telos_plugin_module_instance(module)) ==
            TELOS_PLUGIN_ACTIVE &&
        telos_registry_generation_count(generation) == 2 &&
        telos_registry_generation_find(generation, TELOS_EXTENSION_PROVIDER,
                                       "fixture.responses") != NULL &&
        telos_registry_generation_find(generation, TELOS_EXTENSION_TOOL,
                                       "fixture.echo") != NULL;

    telos_registry_generation_release(generation);
    telos_plugin_module_destroy(module);
    telos_registry_destroy(registry);
    if (!passed) {
        fputs("in-process Plugin did not initialize transactionally\n", stderr);
        return 1;
    }
    return 0;
}
