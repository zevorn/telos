#include <stdint.h>
#include <stdio.h>

#include <telos/registry.h>

static bool commit_one(struct telos_registry *registry,
                       const char *plugin_id,
                       enum telos_extension_kind kind,
                       const char *id)
{
    const struct telos_extension_descriptor descriptor = {
        .id = id,
        .kind = kind,
        .implementation = (const void *)1,
    };
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, plugin_id, NULL);

    return transaction != NULL &&
           telos_registry_transaction_add(transaction, &descriptor, NULL) &&
           telos_registry_transaction_commit(transaction, NULL);
}

static bool invalid_descriptor(struct telos_registry *registry,
                               struct telos_extension_descriptor descriptor)
{
    struct telos_registry_transaction *transaction =
        telos_registry_transaction_begin(registry, "invalid.plugin", NULL);
    struct telos_error *error = NULL;
    bool rejected =
        transaction != NULL &&
        telos_registry_transaction_add(transaction, &descriptor, NULL) &&
        !telos_registry_transaction_commit(transaction, &error) &&
        error != NULL;

    telos_error_release(error);
    telos_registry_transaction_abort(transaction);
    return rejected;
}

int main(void)
{
    const char *capabilities[] = {"filesystem.read"};
    const char *bad_capabilities[] = {NULL};
    const char *empty_capabilities[] = {""};
    struct telos_error *error = NULL;
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_registry_generation *retained;
    struct telos_registry_transaction *transaction;
    struct telos_extension_descriptor descriptor = {
        .id = "valid",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = (const void *)1,
    };
    bool passed =
        telos_registry_create(NULL, 1, &error) == NULL && error != NULL;

    telos_error_release(error);
    error = NULL;
    passed = passed &&
             telos_registry_create(bad_capabilities, 1, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             telos_registry_create(empty_capabilities, 1, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        telos_registry_create(empty_capabilities, SIZE_MAX, &error) == NULL &&
        error != NULL;
    telos_error_release(error);
    error = NULL;

    registry = telos_registry_create(capabilities, 1, NULL);
    generation = telos_registry_acquire(registry);
    retained = telos_registry_generation_retain(generation);
    passed = passed && registry != NULL && generation != NULL &&
             retained == generation && telos_registry_acquire(NULL) == NULL &&
             telos_registry_generation_retain(NULL) == NULL &&
             telos_registry_generation_number(NULL) == 0 &&
             telos_registry_generation_count(NULL) == 0 &&
             telos_registry_generation_at(NULL, 0) == NULL &&
             telos_registry_generation_at(generation, 0) == NULL &&
             telos_registry_generation_find(NULL, TELOS_EXTENSION_TOOL,
                                            "valid") == NULL &&
             telos_registry_generation_find(generation, TELOS_EXTENSION_TOOL,
                                            NULL) == NULL &&
             telos_registry_generation_find(generation, TELOS_EXTENSION_TOOL,
                                            "missing") == NULL;
    telos_registry_generation_release(retained);
    telos_registry_generation_release(generation);
    telos_registry_generation_release(NULL);

    passed = passed &&
             telos_registry_transaction_begin(NULL, "plugin", &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             telos_registry_transaction_begin(registry, NULL, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             telos_registry_transaction_begin(registry, "", &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    transaction =
        telos_registry_transaction_begin(registry, "empty.plugin", NULL);
    passed = passed &&
             !telos_registry_transaction_add(NULL, &descriptor, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             !telos_registry_transaction_add(transaction, NULL, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_registry_transaction_commit(NULL, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             !telos_registry_transaction_commit(transaction, &error) &&
             error != NULL;
    telos_error_release(error);
    telos_registry_transaction_abort(transaction);
    telos_registry_transaction_abort(NULL);

    descriptor.id = "";
    passed = passed && invalid_descriptor(registry, descriptor);
    descriptor.id = "bad-kind-low";
    descriptor.kind = 0;
    passed = passed && invalid_descriptor(registry, descriptor);
    descriptor.id = "bad-kind-high";
    descriptor.kind = TELOS_EXTENSION_PROMPT + 1;
    passed = passed && invalid_descriptor(registry, descriptor);
    descriptor.id = "missing-capability";
    descriptor.kind = TELOS_EXTENSION_TOOL;
    {
        const char *missing[] = {"network.https"};

        descriptor.required_capabilities = missing;
        descriptor.required_capability_count = 1;
        passed = passed && invalid_descriptor(registry, descriptor);
    }
    descriptor.required_capabilities = NULL;
    descriptor.required_capability_count = 0;

    transaction =
        telos_registry_transaction_begin(registry, "duplicate.plugin", NULL);
    descriptor.id = "duplicate";
    passed = passed &&
             telos_registry_transaction_add(transaction, &descriptor, NULL) &&
             telos_registry_transaction_add(transaction, &descriptor, NULL) &&
             !telos_registry_transaction_commit(transaction, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    telos_registry_transaction_abort(transaction);

    passed = passed && commit_one(registry, "first.plugin",
                                  TELOS_EXTENSION_TOOL, "shared");
    transaction =
        telos_registry_transaction_begin(registry, "second.plugin", NULL);
    descriptor.id = "shared";
    passed = passed &&
             telos_registry_transaction_add(transaction, &descriptor, NULL) &&
             !telos_registry_transaction_commit(transaction, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    telos_registry_transaction_abort(transaction);

    passed = passed &&
             commit_one(registry, "second.plugin", TELOS_EXTENSION_PROVIDER,
                        "shared") &&
             commit_one(registry, "first.plugin", TELOS_EXTENSION_TOOL,
                        "replacement");

    transaction =
        telos_registry_transaction_begin(registry, "many.plugin", NULL);
    for (size_t index = 0; passed && index < 6; ++index) {
        static const char *ids[] = {
            "many-0", "many-1", "many-2", "many-3", "many-4", "many-5",
        };

        descriptor.id = ids[index];
        passed = telos_registry_transaction_add(transaction, &descriptor, NULL);
    }
    passed = passed && telos_registry_transaction_commit(transaction, NULL);
    generation = telos_registry_acquire(registry);
    passed = passed && telos_registry_generation_number(generation) == 4 &&
             telos_registry_generation_count(generation) == 8 &&
             telos_registry_generation_at(generation, 7) != NULL &&
             telos_registry_generation_at(generation, 8) == NULL &&
             telos_registry_generation_find(generation, TELOS_EXTENSION_TOOL,
                                            "many-5") != NULL;

    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_registry_destroy(NULL);
    if (!passed) {
        fputs("Registry validation and conflict matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
