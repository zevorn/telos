#include <telos/plugin.h>
#include <telos/plugins/tui_frontend.h>

static const char *const capabilities[] = {
    "tui.interactive",
};

static const struct telos_frontend_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.tui-frontend",
    .run = telos_tui_frontend_run_stdio,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.tui-frontend",
        .kind = TELOS_EXTENSION_FRONTEND,
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
