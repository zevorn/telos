#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/secret.h>

static char *resolve(
    const char *reference,
    const char *target,
    void *context,
    struct telos_error **error
)
{
    const char *secret = context;
    char *copy = malloc(strlen(secret) + 1);

    (void)reference;
    (void)target;
    (void)error;
    if (copy != NULL) {
        strcpy(copy, secret);
    }
    return copy;
}

int main(void)
{
    static const char raw_secret[] = "sk-fixture-never-log";
    const char *capabilities[] = {"secret.use:provider.openai"};
    struct telos_secret_reference *reference =
        telos_secret_reference_create("secret:provider.openai", NULL);
    struct telos_secret_broker *broker = telos_secret_broker_create(
        resolve,
        (void *)raw_secret,
        NULL
    );
    struct telos_secret_material *material = telos_secret_broker_resolve(
        broker,
        reference,
        "provider.openai",
        capabilities,
        1,
        true,
        NULL
    );
    struct telos_error *error = NULL;
    bool passed = reference != NULL
        && broker != NULL
        && material != NULL
        && strcmp(telos_secret_material_data(material), raw_secret) == 0
        && strcmp(
            telos_secret_reference_id(reference),
            "secret:provider.openai"
        ) == 0;

    telos_secret_material_destroy(material);
    material = telos_secret_broker_resolve(
        broker,
        reference,
        "provider.openai",
        capabilities,
        1,
        false,
        &error
    );
    passed = passed && material == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    material = telos_secret_broker_resolve(
        broker,
        reference,
        "provider.other",
        capabilities,
        1,
        true,
        &error
    );
    passed = passed && material == NULL && error != NULL;

    telos_error_release(error);
    telos_secret_material_destroy(material);
    telos_secret_broker_destroy(broker);
    telos_secret_reference_destroy(reference);
    if (!passed) {
        fputs("Secret Broker crossed an unauthorized boundary\n", stderr);
        return 1;
    }
    return 0;
}
