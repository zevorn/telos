#include <errno.h>
#include <stdint.h>

#include <telos/plugin.h>
#include <telos/plugins/ring_store.h>

static struct telos_event_store *
create_store(const struct telos_value *configuration,
             struct telos_error **error)
{
    int64_t capacity;

    if (error != NULL) {
        *error = NULL;
    }
    if (configuration == NULL ||
        telos_value_type(configuration) != TELOS_VALUE_OBJECT ||
        telos_value_count(configuration) != 1 ||
        !telos_value_integer(telos_value_get(configuration, "capacity"),
                             &capacity) ||
        capacity <= 0 || (uint64_t)capacity > SIZE_MAX) {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                                        "Ring Store requires a positive "
                                        "integer capacity",
                                        NULL);
        }
        return NULL;
    }
    return telos_ring_store_create((size_t)capacity, error);
}

static const struct telos_event_store_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.ring-store",
    .create = create_store,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.ring-store",
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
