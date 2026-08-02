#include <telos/plugin.h>
#include <telos/plugins/anthropic.h>

static const char *const capabilities[] = {
    "network.https",
};

static const struct telos_provider_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.anthropic",
    .dispatch = telos_anthropic_provider_dispatch,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.anthropic",
        .kind = TELOS_EXTENSION_PROVIDER,
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
