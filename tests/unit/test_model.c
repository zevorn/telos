#include <assert.h>
#include <errno.h>
#include <string.h>

#include <telos/model.h>

static bool count_models(const struct telos_model_descriptor *model,
                         void *context, struct telos_error **error)
{
    size_t *count = context;

    (void)error;
    assert(model != NULL);
    *count += 1;
    return true;
}

int main(void)
{
    static const struct telos_model_descriptor gpt = {
        .provider = "openai",
        .id = "gpt-5",
        .name = "GPT-5",
        .api = TELOS_MODEL_API_OPENAI_RESPONSES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS,
    };
    static const struct telos_model_descriptor claude = {
        .provider = "anthropic",
        .id = "claude-sonnet",
        .name = "Claude Sonnet",
        .api = TELOS_MODEL_API_ANTHROPIC_MESSAGES,
    };
    struct telos_model_catalog catalog;
    struct telos_error *error = NULL;
    size_t count = 0;

    telos_model_catalog_initialize(&catalog);
    assert(telos_model_catalog_add(&catalog, &gpt, &error));
    assert(error == NULL);
    assert(telos_model_catalog_add(&catalog, &claude, &error));
    assert(error == NULL);
    assert(telos_model_catalog_current(&catalog) == NULL);
    assert(telos_model_catalog_find(&catalog, "openai", "gpt-5") ==
           &catalog.models[0]);
    assert(telos_model_catalog_select_spec(&catalog, "gpt-5", &error));
    assert(error == NULL);
    assert(telos_model_catalog_current(&catalog)->api ==
           TELOS_MODEL_API_OPENAI_RESPONSES);
    assert(telos_model_catalog_select_spec(&catalog, "anthropic/claude-sonnet",
                                           &error));
    assert(error == NULL);
    assert(strcmp(telos_model_catalog_current(&catalog)->provider,
                  "anthropic") == 0);
    assert(telos_model_catalog_visit(&catalog, count_models, &count, &error));
    assert(error == NULL);
    assert(count == 2);

    assert(!telos_model_catalog_select_spec(&catalog, "missing", &error));
    assert(error != NULL);
    assert(telos_error_code(error) == ENOENT);
    telos_error_release(error);
    error = NULL;

    assert(!telos_model_catalog_add(&catalog, &gpt, &error));
    assert(error != NULL);
    assert(telos_error_code(error) == EEXIST);
    telos_error_release(error);
    error = NULL;

    assert(!telos_model_catalog_select_spec(&catalog, "openai/", &error));
    assert(error != NULL);
    assert(telos_error_code(error) == EINVAL);
    telos_error_release(error);
    return 0;
}
