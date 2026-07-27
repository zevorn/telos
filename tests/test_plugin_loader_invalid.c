#include <assert.h>
#include <stddef.h>

#include <telos/plugin.h>

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

static void assert_empty(struct telos_registry *registry)
{
    struct telos_registry_generation *generation =
        telos_registry_acquire(registry);

    assert(generation != NULL);
    assert(telos_registry_generation_count(generation) == 0);
    telos_registry_generation_release(generation);
}

static void assert_load_fails(
    const char *path,
    const char *plugin_id,
    const struct telos_host_api_v1 *host,
    struct telos_registry *registry,
    struct telos_error **error
)
{
    assert(telos_plugin_module_load_inprocess(
        path,
        plugin_id,
        host,
        registry,
        error
    ) == NULL);
    clear_error(error);
}

int main(int argc, char **argv)
{
    struct telos_registry *registry = telos_registry_create(NULL, 0, NULL);
    const struct telos_host_api_v1 host = {
        .abi_version = TELOS_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(host),
    };
    struct telos_host_api_v1 short_host = host;
    struct telos_error *error = NULL;

    assert(argc == 4);
    assert_load_fails(NULL, "fixture", &host, registry, &error);
    assert_load_fails("", "fixture", &host, registry, &error);
    assert_load_fails(argv[1], NULL, &host, registry, &error);
    assert_load_fails(argv[1], "", &host, registry, &error);
    assert_load_fails(argv[1], "fixture", NULL, registry, &error);
    assert_load_fails(argv[1], "fixture", &host, NULL, &error);
    short_host.struct_size = offsetof(struct telos_host_api_v1, log);
    assert_load_fails(
        argv[1],
        "fixture",
        &short_host,
        registry,
        &error
    );
    assert_load_fails(
        "/definitely/missing/telos-plugin.so",
        "fixture",
        &host,
        registry,
        &error
    );
    assert_load_fails(argv[2], "fixture", &host, registry, &error);
    assert_load_fails(argv[3], "fixture", &host, registry, &error);
    assert_empty(registry);

    {
        static int implementation;
        const struct telos_extension_descriptor conflict = {
            .id = "fixture.responses",
            .plugin_id = "existing",
            .kind = TELOS_EXTENSION_PROVIDER,
            .implementation = &implementation,
        };
        struct telos_registry_transaction *transaction =
            telos_registry_transaction_begin(registry, "existing", NULL);
        struct telos_registry_generation *generation;

        assert(telos_registry_transaction_add(
            transaction,
            &conflict,
            NULL
        ));
        assert(telos_registry_transaction_commit(transaction, NULL));
        assert_load_fails(argv[1], "fixture", &host, registry, &error);
        generation = telos_registry_acquire(registry);
        assert(telos_registry_generation_count(generation) == 1);
        assert(telos_registry_generation_find(
            generation,
            TELOS_EXTENSION_PROVIDER,
            "fixture.responses"
        ) != NULL);
        assert(telos_registry_generation_find(
            generation,
            TELOS_EXTENSION_TOOL,
            "fixture.echo"
        ) == NULL);
        telos_registry_generation_release(generation);
    }

    telos_plugin_module_destroy(NULL);
    assert(telos_plugin_module_instance(NULL) == NULL);
    telos_registry_destroy(registry);
    return 0;
}
