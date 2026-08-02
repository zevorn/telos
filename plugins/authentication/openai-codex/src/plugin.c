#include <telos/authentication.h>
#include <telos/plugin.h>
#include <telos/plugins/openai_codex_auth.h>

static const char *const capabilities[] = {
    "filesystem.read",
    "filesystem.write",
    "network.https",
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = TELOS_OPENAI_CODEX_AUTH_ID,
        .kind = TELOS_EXTENSION_AUTHENTICATION,
        .required_capabilities = capabilities,
        .required_capability_count = 3,
        .implementation =
            telos_openai_codex_authentication_definition(),
    };

    if (host == NULL || host->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar == NULL ||
        registrar->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar->add == NULL) {
        return 1;
    }
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
