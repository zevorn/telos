#include <telos/plugin.h>

static int provider_implementation;
static int tool_implementation;

int telos_plugin_init_v1(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
)
{
    const struct telos_extension_descriptor provider = {
        .id = "fixture.responses",
        .kind = TELOS_EXTENSION_PROVIDER,
        .implementation = &provider_implementation,
    };
    const struct telos_extension_descriptor tool = {
        .id = "fixture.echo",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &tool_implementation,
    };

    if (
        host == NULL
        || registrar == NULL
        || host->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar->add == NULL
        || !registrar->add(registrar->context, &provider, NULL)
        || !registrar->add(registrar->context, &tool, NULL)
    ) {
        return 1;
    }
    return 0;
}
