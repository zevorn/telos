#include <errno.h>
#include <string.h>

#include <telos/model.h>

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool model_valid(const struct telos_model_descriptor *model)
{
    return model != NULL && model->provider != NULL &&
           model->provider[0] != '\0' && model->id != NULL &&
           model->id[0] != '\0' && model->name != NULL &&
           model->name[0] != '\0' &&
           (model->api == TELOS_MODEL_API_OPENAI_RESPONSES ||
            model->api == TELOS_MODEL_API_OPENAI_CHAT ||
            model->api == TELOS_MODEL_API_ANTHROPIC_MESSAGES);
}

void telos_model_catalog_initialize(struct telos_model_catalog *catalog)
{
    if (catalog != NULL) {
        memset(catalog, 0, sizeof(*catalog));
    }
}

const struct telos_model_descriptor *
telos_model_catalog_at(const struct telos_model_catalog *catalog, size_t index)
{
    if (catalog == NULL || index >= catalog->count) {
        return NULL;
    }
    return &catalog->models[index];
}

const struct telos_model_descriptor *telos_model_catalog_current(
    const struct telos_model_catalog *catalog)
{
    if (catalog == NULL || !catalog->has_current) {
        return NULL;
    }
    return telos_model_catalog_at(catalog, catalog->current);
}

const struct telos_model_descriptor *telos_model_catalog_find(
    const struct telos_model_catalog *catalog, const char *provider,
    const char *id)
{
    if (catalog == NULL || provider == NULL || id == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < catalog->count; ++index) {
        const struct telos_model_descriptor *model = &catalog->models[index];

        if (strcmp(model->provider, provider) == 0 &&
            strcmp(model->id, id) == 0) {
            return model;
        }
    }
    return NULL;
}

bool telos_model_catalog_add(struct telos_model_catalog *catalog,
                             const struct telos_model_descriptor *model,
                             struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (catalog == NULL || !model_valid(model)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Model descriptor is invalid");
        return false;
    }
    if (catalog->count >= TELOS_MODEL_CATALOG_CAPACITY) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOSPC,
                  "Model catalog is full");
        return false;
    }
    if (telos_model_catalog_find(catalog, model->provider, model->id) != NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EEXIST,
                  "Model is already registered");
        return false;
    }
    catalog->models[catalog->count++] = *model;
    return true;
}

bool telos_model_catalog_select(struct telos_model_catalog *catalog,
                                const char *provider, const char *id,
                                struct telos_error **error)
{
    const struct telos_model_descriptor *model;

    if (error != NULL) {
        *error = NULL;
    }
    model = telos_model_catalog_find(catalog, provider, id);
    if (model == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                  "Model is not registered");
        return false;
    }
    catalog->current = (size_t)(model - catalog->models);
    catalog->has_current = true;
    return true;
}

bool telos_model_catalog_select_spec(struct telos_model_catalog *catalog,
                                     const char *spec,
                                     struct telos_error **error)
{
    const char *separator;
    const struct telos_model_descriptor *match = NULL;
    size_t matches = 0;

    if (error != NULL) {
        *error = NULL;
    }
    if (catalog == NULL || spec == NULL || spec[0] == '\0') {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Model selection is invalid");
        return false;
    }
    separator = strchr(spec, '/');
    if (separator != NULL) {
        char provider[128];
        size_t provider_size = (size_t)(separator - spec);

        if (provider_size == 0 || provider_size >= sizeof(provider) ||
            separator[1] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Model selection is invalid");
            return false;
        }
        memcpy(provider, spec, provider_size);
        provider[provider_size] = '\0';
        return telos_model_catalog_select(catalog, provider, separator + 1,
                                          error);
    }
    for (size_t index = 0; index < catalog->count; ++index) {
        if (strcmp(catalog->models[index].id, spec) == 0) {
            match = &catalog->models[index];
            ++matches;
        }
    }
    if (matches == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                  "Model is not registered");
        return false;
    }
    if (matches > 1) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EEXIST,
                  "Model name is ambiguous; use provider/model");
        return false;
    }
    catalog->current = (size_t)(match - catalog->models);
    catalog->has_current = true;
    return true;
}

bool telos_model_catalog_visit(const struct telos_model_catalog *catalog,
                               telos_model_visit_fn visit, void *context,
                               struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (catalog == NULL || visit == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Model catalog visitor is invalid");
        return false;
    }
    for (size_t index = 0; index < catalog->count; ++index) {
        if (!visit(&catalog->models[index], context, error)) {
            return false;
        }
    }
    return true;
}
