#include <stdio.h>

#include <telos/registry.h>

int main(void)
{
    const char *capabilities[] = {"filesystem.read"};
    struct telos_registry *registry =
        telos_registry_create(capabilities, 1, NULL);
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "dev.zevorn.invalid", NULL);
    const struct telos_extension_descriptor valid = {
        .id = "dev.zevorn.valid",
        .plugin_id = "dev.zevorn.invalid",
        .kind = TELOS_EXTENSION_TOOL,
    };
    const char *missing[] = {"network.https"};
    const struct telos_extension_descriptor invalid = {
        .id = "dev.zevorn.provider",
        .plugin_id = "dev.zevorn.invalid",
        .kind = TELOS_EXTENSION_PROVIDER,
        .required_capabilities = missing,
        .required_capability_count = 1,
    };
    struct telos_error *error = NULL;
    struct telos_registry_generation *generation;

    if (registry == NULL || transaction == NULL ||
        !telos_registry_transaction_add(transaction, &valid, NULL) ||
        !telos_registry_transaction_add(transaction, &invalid, NULL) ||
        telos_registry_transaction_commit(transaction, &error) ||
        error == NULL) {
        fputs("invalid Registry transaction did not fail\n", stderr);
        telos_error_release(error);
        telos_registry_transaction_abort(transaction);
        telos_registry_destroy(registry);
        return 1;
    }
    telos_error_release(error);
    telos_registry_transaction_abort(transaction);

    generation = telos_registry_acquire(registry);
    if (generation == NULL ||
        telos_registry_generation_number(generation) != 0 ||
        telos_registry_generation_count(generation) != 0) {
        fputs("failed transaction partially changed Registry\n", stderr);
        telos_registry_generation_release(generation);
        telos_registry_destroy(registry);
        return 1;
    }

    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    return 0;
}
