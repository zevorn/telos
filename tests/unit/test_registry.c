#include <stdio.h>

#include <telos/registry.h>

int main(void)
{
    const char *capabilities[] = {"network.https", "filesystem.read"};
    struct telos_registry *registry =
        telos_registry_create(capabilities, 2, NULL);
    struct telos_registry_generation *original =
        telos_registry_acquire(registry);
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "dev.zevorn.bundle", NULL);
    const char *provider_capabilities[] = {"network.https"};
    const struct telos_extension_descriptor provider = {
        .id = "dev.zevorn.responses",
        .plugin_id = "dev.zevorn.bundle",
        .kind = TELOS_EXTENSION_PROVIDER,
        .required_capabilities = provider_capabilities,
        .required_capability_count = 1,
        .implementation = (const void *)1,
    };
    const struct telos_extension_descriptor tool = {
        .id = "dev.zevorn.echo",
        .plugin_id = "dev.zevorn.bundle",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = (const void *)2,
    };
    struct telos_registry_generation *published;
    bool passed;

    passed = registry != NULL && original != NULL && transaction != NULL &&
             telos_registry_generation_number(original) == 0 &&
             telos_registry_generation_count(original) == 0 &&
             telos_registry_transaction_add(transaction, &provider, NULL) &&
             telos_registry_transaction_add(transaction, &tool, NULL) &&
             telos_registry_transaction_commit(transaction, NULL);

    published = telos_registry_acquire(registry);
    passed = passed && published != NULL &&
             telos_registry_generation_number(published) == 1 &&
             telos_registry_generation_count(published) == 2 &&
             telos_registry_generation_count(original) == 0 &&
             telos_registry_generation_find(published, TELOS_EXTENSION_PROVIDER,
                                            "dev.zevorn.responses") != NULL &&
             telos_registry_generation_find(published, TELOS_EXTENSION_TOOL,
                                            "dev.zevorn.echo") != NULL;

    telos_registry_generation_release(published);
    telos_registry_generation_release(original);
    telos_registry_destroy(registry);

    if (!passed) {
        fputs("Registry did not publish an immutable transaction\n", stderr);
        return 1;
    }
    return 0;
}
