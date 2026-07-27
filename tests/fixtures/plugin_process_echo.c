#include <telos/plugin.h>
#include <telos/tool.h>

static const struct telos_value_api_v1 *value_api;

static bool execute(
    const struct telos_tool_context *context,
    const struct telos_value *arguments,
    struct telos_value **result,
    struct telos_error **error
)
{
    (void)context;
    (void)error;
    *result = value_api->retain(arguments);
    return *result != NULL;
}

static const struct telos_tool_definition tool = {
    .id = "dev.zevorn.process-echo",
    .execute = execute,
};

int telos_plugin_init_v1(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.process-echo",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &tool,
    };

    if (
        host == NULL
        || host->abi_version != TELOS_PLUGIN_ABI_VERSION
        || host->value == NULL
        || registrar == NULL
        || registrar->abi_version != TELOS_PLUGIN_ABI_VERSION
        || registrar->add == NULL
    ) {
        return 1;
    }
    value_api = host->value;
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
