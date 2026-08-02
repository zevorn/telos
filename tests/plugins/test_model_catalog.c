#include <assert.h>
#include <string.h>

#include <telos/plugins/model_catalog.h>

static bool count_model(const struct telos_model_descriptor *model,
                        void *context, struct telos_error **error)
{
    size_t *count = context;

    (void)error;
    assert(model->provider != NULL);
    assert(model->id != NULL);
    *count += 1;
    return true;
}

int main(void)
{
    struct telos_model_catalog catalog;
    struct telos_error *error = NULL;
    size_t count = 0;

    telos_model_catalog_initialize(&catalog);
    assert(telos_official_model_catalog_add(&catalog, &error));
    assert(error == NULL);
    assert(telos_model_catalog_visit(&catalog, count_model, &count, &error));
    assert(error == NULL);
    assert(count >= 8);
    assert(telos_model_catalog_find(&catalog, "openai", "gpt-5") != NULL);
    assert(telos_model_catalog_find(&catalog, "deepseek",
                                    "deepseek-reasoner") != NULL);
    assert(telos_model_catalog_find(&catalog, "zai", "glm-4.7") != NULL);
    assert(telos_model_catalog_find(&catalog, "anthropic",
                                    "claude-sonnet-4-5") != NULL);
    assert(!telos_official_model_catalog_add(&catalog, &error));
    assert(error != NULL);
    telos_error_release(error);
    return 0;
}
