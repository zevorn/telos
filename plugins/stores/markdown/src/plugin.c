#include <errno.h>

#include <telos/plugin.h>
#include <telos/plugins/markdown_store.h>

static struct telos_event_store *
create_store(const struct telos_value *configuration,
             struct telos_error **error)
{
    const char *path = NULL;

    if (error != NULL) {
        *error = NULL;
    }
    if (configuration != NULL &&
        telos_value_type(configuration) == TELOS_VALUE_OBJECT) {
        path = telos_value_string(telos_value_get(configuration, "path"));
    }
    if (path == NULL || path[0] == '\0') {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                                        "Markdown Store requires a path", NULL);
        }
        return NULL;
    }
    return telos_markdown_store_create(path, error);
}

static const char *const capabilities[] = {
    "filesystem.read",
    "filesystem.write",
};

static const struct telos_event_store_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.markdown-store",
    .create = create_store,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.markdown-store",
        .kind = TELOS_EXTENSION_STORE,
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
