#include <telos/plugin.h>

static int provider_implementation;

int telos_plugin_init_v1(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
)
{
    const char *permissions[] = {"network.https"};
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.example.provider",
        .kind = TELOS_EXTENSION_PROVIDER,
        .required_capabilities = permissions,
        .required_capability_count = 1,
        .implementation = &provider_implementation,
    };

    if (
        host == NULL
        || host->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar == NULL
        || registrar->add == NULL
    ) {
        return 1;
    }
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
