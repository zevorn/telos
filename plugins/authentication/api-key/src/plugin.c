#include <telos/plugin.h>
#include <telos/plugins/api_key_auth.h>

static const char *const capabilities[] = {
    "filesystem.read",
    "filesystem.write",
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptors[] = {
        {
            .id = "dev.zevorn.deepseek-api-key-auth",
            .kind = TELOS_EXTENSION_AUTHENTICATION,
            .required_capabilities = capabilities,
            .required_capability_count = 2,
            .implementation =
                telos_deepseek_api_key_authentication_definition(),
        },
        {
            .id = "dev.zevorn.zai-api-key-auth",
            .kind = TELOS_EXTENSION_AUTHENTICATION,
            .required_capabilities = capabilities,
            .required_capability_count = 2,
            .implementation = telos_zai_api_key_authentication_definition(),
        },
        {
            .id = "dev.zevorn.anthropic-api-key-auth",
            .kind = TELOS_EXTENSION_AUTHENTICATION,
            .required_capabilities = capabilities,
            .required_capability_count = 2,
            .implementation =
                telos_anthropic_api_key_authentication_definition(),
        },
    };

    if (host == NULL || host->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar == NULL ||
        registrar->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar->add == NULL) {
        return 1;
    }
    for (size_t index = 0;
         index < sizeof(descriptors) / sizeof(descriptors[0]); ++index) {
        if (!registrar->add(registrar->context, &descriptors[index], NULL)) {
            return 1;
        }
    }
    return 0;
}
