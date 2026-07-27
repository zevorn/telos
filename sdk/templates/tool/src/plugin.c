#include <telos/plugin.h>

static int echo_tool_implementation;

int telos_plugin_init_v1(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.example.echo-tool",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &echo_tool_implementation,
    };

    if (
        host == NULL
        || host->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar == NULL
        || registrar->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar->add == NULL
    ) {
        return 1;
    }
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
