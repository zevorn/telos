#include <telos/plugin.h>

static int implementation;

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "fixture.partial",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &implementation,
    };

    if (host == NULL || registrar == NULL || registrar->add == NULL) {
        return 1;
    }
    if (!registrar->add(registrar->context, &descriptor, NULL)) {
        return 1;
    }
    return 1;
}
