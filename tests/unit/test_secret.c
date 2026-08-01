#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/secret.h>

static char *resolve(const char *reference,
                     const char *target,
                     void *context,
                     struct telos_error **error)
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

static char *resolve_missing(const char *reference,
                             const char *target,
                             void *context,
                             struct telos_error **error)
{
    (void)reference;
    (void)target;
    (void)context;
    (void)error;
    return NULL;
}

static char *resolve_error(const char *reference,
                           const char *target,
                           void *context,
                           struct telos_error **error)
{
    (void)reference;
    (void)target;
    (void)context;
    *error =
        telos_error_create(TELOS_ERROR_DOMAIN_IO, EIO, "fixture failure", NULL);
    return NULL;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

int main(void)
{
    static const char raw_secret[] = "sk-fixture-never-log";
    const char *capabilities[] = {"secret.use:provider.openai"};
    const char *unrelated[] = {NULL, "secret.use:provider.other"};
    struct telos_secret_reference *reference =
        telos_secret_reference_create("secret:provider.openai", NULL);
    struct telos_secret_broker *broker =
        telos_secret_broker_create(resolve, (void *)raw_secret, NULL);
    struct telos_secret_material *material = telos_secret_broker_resolve(
        broker, reference, "provider.openai", capabilities, 1, true, NULL);
    struct telos_error *error = NULL;
    bool passed =
        reference != NULL && broker != NULL && material != NULL &&
        strcmp(telos_secret_material_data(material), raw_secret) == 0 &&
        strcmp(telos_secret_reference_id(reference),
               "secret:provider.openai") == 0;

    telos_secret_material_destroy(material);
    material = telos_secret_broker_resolve(broker, reference, "provider.openai",
                                           capabilities, 1, false, &error);
    passed = passed && material == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    material = telos_secret_broker_resolve(broker, reference, "provider.other",
                                           capabilities, 1, true, &error);
    passed = passed && material == NULL && error != NULL;

    telos_error_release(error);
    error = NULL;

    assert(telos_secret_reference_create(NULL, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_reference_create("provider.openai", &error) == NULL);
    clear_error(&error);
    assert(telos_secret_reference_create("secret:", &error) == NULL);
    clear_error(&error);
    assert(telos_secret_reference_id(NULL) == NULL);
    telos_secret_reference_destroy(NULL);

    assert(telos_secret_broker_create(NULL, NULL, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(NULL, reference, "provider.openai",
                                       capabilities, 1, true, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(broker, NULL, "provider.openai",
                                       capabilities, 1, true, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(broker, reference, NULL, capabilities, 1,
                                       true, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(broker, reference, "provider.openai",
                                       NULL, 1, true, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(broker, reference, "provider.openai",
                                       NULL, 0, true, &error) == NULL);
    clear_error(&error);
    assert(telos_secret_broker_resolve(broker, reference, "provider.openai",
                                       unrelated, 2, true, &error) == NULL);
    clear_error(&error);
    {
        struct telos_secret_broker *missing =
            telos_secret_broker_create(resolve_missing, NULL, NULL);
        struct telos_secret_broker *failing =
            telos_secret_broker_create(resolve_error, NULL, NULL);

        assert(telos_secret_broker_resolve(missing, reference,
                                           "provider.openai", capabilities, 1,
                                           true, &error) == NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_IO);
        clear_error(&error);
        assert(telos_secret_broker_resolve(failing, reference,
                                           "provider.openai", capabilities, 1,
                                           true, &error) == NULL);
        assert(strcmp(telos_error_message(error), "fixture failure") == 0);
        clear_error(&error);
        telos_secret_broker_destroy(failing);
        telos_secret_broker_destroy(missing);
    }
    assert(telos_secret_material_data(NULL) == NULL);
    telos_secret_material_destroy(NULL);
    telos_secret_broker_destroy(NULL);
    telos_secret_material_destroy(material);
    telos_secret_broker_destroy(broker);
    telos_secret_reference_destroy(reference);
    if (!passed) {
        fputs("Secret Broker crossed an unauthorized boundary\n", stderr);
        return 1;
    }
    return 0;
}
