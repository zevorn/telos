#include <errno.h>
#include <stddef.h>

#include <telos/plugin.h>
#include <telos/plugins/memory_store.h>

static struct telos_event_store *
create_store(const struct telos_value *configuration,
             struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (configuration != NULL &&
        (telos_value_type(configuration) != TELOS_VALUE_OBJECT ||
         telos_value_count(configuration) != 0)) {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                                        "Memory Store configuration must be an "
                                        "empty object",
                                        NULL);
        }
        return NULL;
    }
    return telos_memory_store_create(error);
}

static const struct telos_event_store_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.memory-store",
    .create = create_store,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.memory-store",
        .kind = TELOS_EXTENSION_STORE,
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
