#include <telos/plugin.h>
#include <telos/plugins/posix_tools.h>
#include <telos/tool.h>

static const char *const capabilities[] = {
    "filesystem.read",
    "filesystem.write",
    "process.spawn",
};

static const struct telos_tool_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.posix-tools",
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.posix-tools",
        .kind = TELOS_EXTENSION_TOOL,
        .required_capabilities = capabilities,
        .required_capability_count = 3,
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
