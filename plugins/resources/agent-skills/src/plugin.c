#include <telos/plugin.h>
#include <telos/resource.h>

static const char *const capabilities[] = {
    "filesystem.read",
};

static const struct telos_resource_source_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.agent-skills",
    .create = telos_resource_manager_create,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.agent-skills",
        .kind = TELOS_EXTENSION_CONTEXT_SOURCE,
        .required_capabilities = capabilities,
        .required_capability_count = 1,
        .implementation = &definition,
    };

    if (host == NULL || host->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar == NULL ||
        registrar->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar->add == NULL) {
        return 1;
    }
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
