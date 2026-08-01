#include <telos/plugin.h>
#include <telos/plugins/curl_transport.h>

static const char *const capabilities[] = {
    "network.https",
    "network.http:loopback",
};

static const struct telos_transport_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.curl-transport",
    .send = telos_curl_transport_send,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.curl-transport",
        .kind = TELOS_EXTENSION_TRANSPORT,
        .required_capabilities = capabilities,
        .required_capability_count = 2,
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
