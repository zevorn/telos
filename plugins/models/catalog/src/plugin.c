#include <telos/plugin.h>
#include <telos/plugins/model_catalog.h>

static const struct telos_model_catalog_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.model-catalog",
    .add = telos_official_model_catalog_add,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.model-catalog",
        .kind = TELOS_EXTENSION_MODEL_CATALOG,
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
