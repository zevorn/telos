#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugin.h>

struct telos_plugin_module {
    void *handle;
    struct telos_plugin_instance *instance;
};

struct registrar_context {
    struct telos_registry_transaction *transaction;
};

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool registrar_add(
    void *context,
    const struct telos_extension_descriptor *descriptor,
    struct telos_error **error
)
{
    struct registrar_context *registrar = context;

    return telos_registry_transaction_add(
        registrar->transaction,
        descriptor,
        error
    );
}

struct telos_plugin_module *telos_plugin_module_load_inprocess(
    const char *path,
    const char *plugin_id,
    const struct telos_host_api_v1 *host,
    struct telos_registry *registry,
    struct telos_error **error
)
{
    struct telos_plugin_module *module = NULL;
    struct telos_registry_transaction *transaction = NULL;
    struct registrar_context registrar_context;
    struct telos_plugin_registrar_v1 registrar = {
        .abi_version = TELOS_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(registrar),
        .context = &registrar_context,
        .add = registrar_add,
    };
    telos_plugin_init_v1_fn initialize;
    void *symbol;

    if (error != NULL) {
        *error = NULL;
    }
    if (
        path == NULL
        || path[0] == '\0'
        || plugin_id == NULL
        || plugin_id[0] == '\0'
        || host == NULL
        || registry == NULL
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "in-process Plugin load arguments are invalid"
        );
        return NULL;
    }
    if (
        host->abi_version != TELOS_PLUGIN_ABI_VERSION
        || host->struct_size < offsetof(struct telos_host_api_v1, log)
            + sizeof(host->log)
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PLUGIN,
            EPROTONOSUPPORT,
            "Plugin Host ABI is incompatible"
        );
        return NULL;
    }

    module = calloc(1, sizeof(*module));
    if (module == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin module allocation failed"
        );
        return NULL;
    }
    module->instance = telos_plugin_instance_create(plugin_id, error);
    if (module->instance == NULL) {
        free(module);
        return NULL;
    }
    if (
        !telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_VERIFIED,
            error
        )
    ) {
        goto failure;
    }

    module->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (module->handle == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PLUGIN,
            ENOENT,
            dlerror()
        );
        goto failure;
    }
    if (
        !telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_LOADED,
            error
        )
    ) {
        goto failure;
    }

    symbol = dlsym(module->handle, "telos_plugin_init_v1");
    if (symbol == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PLUGIN,
            ENOSYS,
            "Plugin entry telos_plugin_init_v1 was not found"
        );
        goto failure;
    }
    _Static_assert(
        sizeof(initialize) == sizeof(symbol),
        "POSIX function and object pointers must have equal size"
    );
    memcpy(&initialize, &symbol, sizeof(initialize));

    transaction = telos_registry_transaction_begin(
        registry,
        plugin_id,
        error
    );
    if (transaction == NULL) {
        goto failure;
    }
    registrar_context.transaction = transaction;
    if (
        !telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_INITIALIZING,
            error
        )
        || initialize(host, &registrar) != 0
    ) {
        if (error == NULL || *error == NULL) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_PLUGIN,
                EPROTO,
                "Plugin initialization failed"
            );
        }
        goto failure;
    }
    if (
        !telos_plugin_instance_set_healthy(module->instance, true, error)
        || !telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_ACTIVE,
            error
        )
        || !telos_registry_transaction_commit(transaction, error)
    ) {
        goto failure;
    }
    transaction = NULL;
    return module;

failure:
    telos_registry_transaction_abort(transaction);
    if (module->handle != NULL) {
        dlclose(module->handle);
    }
    telos_plugin_instance_release(module->instance);
    free(module);
    return NULL;
}

void telos_plugin_module_destroy(struct telos_plugin_module *module)
{
    bool can_unload = true;

    if (module == NULL) {
        return;
    }
    if (
        telos_plugin_instance_state(module->instance) == TELOS_PLUGIN_ACTIVE
    ) {
        can_unload = telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_QUIESCING,
            NULL
        );
    }
    if (
        can_unload
        && telos_plugin_instance_state(module->instance)
            == TELOS_PLUGIN_QUIESCING
    ) {
        can_unload = telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_STOPPED,
            NULL
        );
    }
    if (
        can_unload
        && telos_plugin_instance_state(module->instance)
            == TELOS_PLUGIN_STOPPED
    ) {
        can_unload = telos_plugin_instance_transition(
            module->instance,
            TELOS_PLUGIN_UNLOADED,
            NULL
        );
    }
    if (can_unload && module->handle != NULL) {
        dlclose(module->handle);
    }
    telos_plugin_instance_release(module->instance);
    free(module);
}

const struct telos_plugin_instance *telos_plugin_module_instance(
    const struct telos_plugin_module *module
)
{
    return module == NULL ? NULL : module->instance;
}
