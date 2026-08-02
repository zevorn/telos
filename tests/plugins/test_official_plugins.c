#define _XOPEN_SOURCE 700

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <telos/manifest.h>
#include <telos/model.h>
#include <telos/plugin.h>
#include <telos/authentication.h>
#include <telos/frontend.h>
#include <telos/plugins/project_guidance.h>
#include <telos/provider.h>
#include <telos/resource.h>
#include <telos/store.h>
#include <telos/transport.h>

static void discard_log(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    (void)message;
}

static bool
accept_descriptor(void *context,
                  const struct telos_extension_descriptor *descriptor,
                  struct telos_error **error)
{
    (void)context;
    (void)error;
    return descriptor != NULL;
}

static void verify_entry_validation(const char *path,
                                    const struct telos_host_api_v1 *host)
{
    struct telos_plugin_registrar_v1 registrar = {
        .abi_version = TELOS_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(registrar),
        .add = accept_descriptor,
    };
    struct telos_host_api_v1 incompatible_host = *host;
    telos_plugin_init_v1_fn initialize;
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void *symbol;

    assert(module != NULL);
    symbol = dlsym(module, "telos_plugin_init_v1");
    assert(symbol != NULL);
    _Static_assert(sizeof(initialize) == sizeof(symbol),
                   "POSIX function and object pointers must have equal size");
    memcpy(&initialize, &symbol, sizeof(initialize));

    assert(initialize(NULL, &registrar) != 0);
    incompatible_host.abi_version = TELOS_PLUGIN_ABI_VERSION + 1;
    assert(initialize(&incompatible_host, &registrar) != 0);
    assert(initialize(host, NULL) != 0);
    registrar.abi_version = TELOS_PLUGIN_ABI_VERSION + 1;
    assert(initialize(host, &registrar) != 0);
    registrar.abi_version = TELOS_PLUGIN_ABI_VERSION;
    registrar.add = NULL;
    assert(initialize(host, &registrar) != 0);
    assert(dlclose(module) == 0);
}

static const struct telos_extension_descriptor *
find_extension(const struct telos_registry_generation *generation,
               enum telos_extension_kind kind,
               const char *id)
{
    const struct telos_extension_descriptor *descriptor =
        telos_registry_generation_find(generation, kind, id);

    assert(descriptor != NULL);
    assert(descriptor->implementation != NULL);
    return descriptor;
}

static void verify_store(const struct telos_registry_generation *generation,
                         const char *id,
                         const struct telos_value *configuration)
{
    const struct telos_extension_descriptor *descriptor =
        find_extension(generation, TELOS_EXTENSION_STORE, id);
    const struct telos_event_store_definition_v1 *definition =
        descriptor->implementation;
    struct telos_event_store *store;

    assert(definition->struct_size >= sizeof(*definition));
    assert(strcmp(definition->id, id) == 0);
    assert(definition->create != NULL);
    store = definition->create(configuration, NULL);
    assert(store != NULL);
    assert(telos_event_store_count(store) == 0);
    telos_event_store_destroy(store);
}

static void
reject_store_configuration(const struct telos_registry_generation *generation,
                           const char *id,
                           const struct telos_value *configuration)
{
    const struct telos_extension_descriptor *descriptor =
        find_extension(generation, TELOS_EXTENSION_STORE, id);
    const struct telos_event_store_definition_v1 *definition =
        descriptor->implementation;
    struct telos_error *error = NULL;
    struct telos_event_store *store = definition->create(configuration, &error);

    assert(store == NULL);
    assert(error != NULL);
    telos_error_release(error);
}

static void verify_package(const char *directory, const char *id)
{
    char manifest_path[PATH_MAX];
    char lock_path[PATH_MAX];
    struct telos_plugin_manifest *manifest;
    struct telos_plugin_lock *lock;

    assert(snprintf(manifest_path, sizeof(manifest_path), "%s/plugin.toml",
                    directory) < (int)sizeof(manifest_path));
    assert(snprintf(lock_path, sizeof(lock_path), "%s/telos.lock", directory) <
           (int)sizeof(lock_path));
    manifest = telos_plugin_manifest_load(manifest_path, NULL);
    assert(manifest != NULL);
    assert(strcmp(telos_plugin_manifest_id(manifest), id) == 0);
    assert(telos_plugin_manifest_abi(manifest) == TELOS_PLUGIN_ABI_VERSION);
    assert(telos_plugin_manifest_runtime_modes(manifest) &
           TELOS_PLUGIN_RUNTIME_BUILTIN);
    telos_plugin_manifest_destroy(manifest);

    lock = telos_plugin_lock_load(lock_path, NULL);
    assert(lock != NULL);
    assert(telos_plugin_lock_verify(lock, directory, NULL));
    assert(telos_plugin_lock_verify_source(lock, directory, NULL));
    telos_plugin_lock_destroy(lock);
}

int main(int argc, char **argv)
{
    const char *capabilities[] = {
        "filesystem.read",
        "filesystem.write",
        "network.https",
        "secret.use:provider.openai",
        "terminal.interactive",
        "network.http:loopback",
    };
    const char *plugin_ids[] = {
        "dev.zevorn.memory-store",   "dev.zevorn.ring-store",
        "dev.zevorn.markdown-store", "dev.zevorn.openai-responses",
        "dev.zevorn.openai-chat",    "dev.zevorn.anthropic",
        "dev.zevorn.agent-skills",
        "dev.zevorn.project-guidance",
        "dev.zevorn.openai-codex-auth",
        "dev.zevorn.terminal-frontend", "dev.zevorn.curl-transport",
        "dev.zevorn.model-catalog",
    };
    size_t plugin_count;
    struct telos_registry *registry;
    struct telos_host_api_v1 host;
    struct telos_plugin_module *modules[12] = {0};
    struct telos_registry_generation *generation;
    struct telos_value *empty = telos_value_new_object(NULL, NULL, 0);
    struct telos_value *capacity = telos_value_new_integer(2);
    const char *ring_keys[] = {"capacity"};
    const struct telos_value *ring_values[] = {capacity};
    struct telos_value *ring =
        telos_value_new_object(ring_keys, ring_values, 1);
    struct telos_value *zero = telos_value_new_integer(0);
    const struct telos_value *zero_values[] = {zero};
    struct telos_value *zero_ring =
        telos_value_new_object(ring_keys, zero_values, 1);
    struct telos_value *text_capacity = telos_value_new_string("two");
    const struct telos_value *text_capacity_values[] = {text_capacity};
    struct telos_value *text_ring =
        telos_value_new_object(ring_keys, text_capacity_values, 1);
    char path[] = "/tmp/telos-official-plugin-XXXXXX";
    int descriptor = mkstemp(path);
    struct telos_value *path_value = telos_value_new_string(path);
    const char *markdown_keys[] = {"path"};
    const struct telos_value *markdown_values[] = {path_value};
    struct telos_value *markdown =
        telos_value_new_object(markdown_keys, markdown_values, 1);
    struct telos_value *empty_path = telos_value_new_string("");
    const struct telos_value *empty_path_values[] = {empty_path};
    struct telos_value *invalid_markdown =
        telos_value_new_object(markdown_keys, empty_path_values, 1);

    assert(argc >= 19 && argc <= 25 && argc % 2 == 1);
    plugin_count = ((size_t)argc - 1) / 2;
    assert(descriptor >= 0);
    close(descriptor);
    assert(telos_host_api_v1_initialize(&host, NULL, discard_log, NULL));
    registry = telos_registry_create(capabilities, 6, NULL);
    assert(registry != NULL);
    for (size_t index = 0; index < plugin_count; ++index) {
        verify_package(argv[index + plugin_count + 1], plugin_ids[index]);
        verify_entry_validation(argv[index + 1], &host);
        modules[index] = telos_plugin_module_load_inprocess(
            argv[index + 1], plugin_ids[index], &host, registry, NULL);
        assert(modules[index] != NULL);
    }

    generation = telos_registry_acquire(registry);
    assert(generation != NULL);
    verify_store(generation, plugin_ids[0], empty);
    verify_store(generation, plugin_ids[1], ring);
    verify_store(generation, plugin_ids[2], markdown);
    reject_store_configuration(generation, plugin_ids[0], capacity);
    reject_store_configuration(generation, plugin_ids[0], ring);
    reject_store_configuration(generation, plugin_ids[1], NULL);
    reject_store_configuration(generation, plugin_ids[1], empty);
    reject_store_configuration(generation, plugin_ids[1], text_ring);
    reject_store_configuration(generation, plugin_ids[1], zero_ring);
    reject_store_configuration(generation, plugin_ids[2], NULL);
    reject_store_configuration(generation, plugin_ids[2], capacity);
    reject_store_configuration(generation, plugin_ids[2], empty);
    reject_store_configuration(generation, plugin_ids[2], invalid_markdown);
    {
        const struct telos_extension_descriptor *provider =
            find_extension(generation, TELOS_EXTENSION_PROVIDER, plugin_ids[3]);
        const struct telos_provider_definition_v1 *definition =
            provider->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[3]) == 0);
        assert(definition->dispatch != NULL);
    }
    {
        const struct telos_extension_descriptor *provider =
            find_extension(generation, TELOS_EXTENSION_PROVIDER, plugin_ids[4]);
        const struct telos_provider_definition_v1 *definition =
            provider->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[4]) == 0);
        assert(definition->dispatch != NULL);
    }
    {
        const struct telos_extension_descriptor *provider =
            find_extension(generation, TELOS_EXTENSION_PROVIDER, plugin_ids[5]);
        const struct telos_provider_definition_v1 *definition =
            provider->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[5]) == 0);
        assert(definition->dispatch != NULL);
    }
    {
        const struct telos_extension_descriptor *source = find_extension(
            generation, TELOS_EXTENSION_CONTEXT_SOURCE, plugin_ids[6]);
        const struct telos_resource_source_definition_v1 *definition =
            source->implementation;
        struct telos_resource_manager *manager;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[6]) == 0);
        assert(definition->create != NULL);
        manager = definition->create(NULL, 0, NULL);
        assert(manager != NULL);
        telos_resource_manager_destroy(manager);
    }
    {
        const struct telos_extension_descriptor *source = find_extension(
            generation, TELOS_EXTENSION_CONTEXT_SOURCE, plugin_ids[7]);
        const struct telos_project_guidance_definition_v1 *definition =
            source->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[7]) == 0);
        assert(definition->discover != NULL);
        assert(definition->free_string != NULL);
    }
    if (plugin_count > 8) {
        const struct telos_extension_descriptor *authentication =
            find_extension(generation, TELOS_EXTENSION_AUTHENTICATION,
                           plugin_ids[8]);
        const struct telos_authentication_definition_v1 *definition =
            authentication->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[8]) == 0);
        assert(definition->create != NULL);
        assert(definition->destroy != NULL);
        assert(definition->login != NULL);
        assert(definition->logout != NULL);
        assert(definition->status != NULL);
        assert(definition->resolve != NULL);
    }
    if (plugin_count > 9) {
        const struct telos_extension_descriptor *frontend = find_extension(
            generation, TELOS_EXTENSION_FRONTEND, plugin_ids[9]);
        const struct telos_frontend_definition_v1 *definition =
            frontend->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[9]) == 0);
        assert(definition->run != NULL);
    }
    if (plugin_count > 10) {
        const struct telos_extension_descriptor *transport = find_extension(
            generation, TELOS_EXTENSION_TRANSPORT, plugin_ids[10]);
        const struct telos_transport_definition_v1 *definition =
            transport->implementation;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[10]) == 0);
        assert(definition->send != NULL);
    }
    if (plugin_count > 11) {
        const struct telos_extension_descriptor *catalog = find_extension(
            generation, TELOS_EXTENSION_MODEL_CATALOG, plugin_ids[11]);
        const struct telos_model_catalog_definition_v1 *definition =
            catalog->implementation;
        struct telos_model_catalog models;

        assert(definition->struct_size >= sizeof(*definition));
        assert(strcmp(definition->id, plugin_ids[11]) == 0);
        assert(definition->add != NULL);
        telos_model_catalog_initialize(&models);
        assert(definition->add(&models, NULL));
        assert(telos_model_catalog_current(&models) == NULL);
        assert(telos_model_catalog_find(&models, "openai", "gpt-5") !=
               NULL);
    }

    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    for (size_t index = plugin_count; index > 0; --index) {
        telos_plugin_module_destroy(modules[index - 1]);
    }
    telos_value_release(invalid_markdown);
    telos_value_release(empty_path);
    telos_value_release(markdown);
    telos_value_release(path_value);
    telos_value_release(text_ring);
    telos_value_release(text_capacity);
    telos_value_release(zero_ring);
    telos_value_release(zero);
    telos_value_release(ring);
    telos_value_release(capacity);
    telos_value_release(empty);
    unlink(path);
    return 0;
}
