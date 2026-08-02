#ifndef TELOS_MODEL_H
#define TELOS_MODEL_H

#include <telos/error.h>
#include <telos/types.h>

#define TELOS_MODEL_CATALOG_CAPACITY 64U

enum telos_model_api {
    TELOS_MODEL_API_OPENAI_RESPONSES = 1,
    TELOS_MODEL_API_OPENAI_CHAT,
    TELOS_MODEL_API_ANTHROPIC_MESSAGES,
};

enum telos_model_capability {
    TELOS_MODEL_CAPABILITY_STREAMING = 1U << 0,
    TELOS_MODEL_CAPABILITY_TOOLS = 1U << 1,
    TELOS_MODEL_CAPABILITY_REASONING = 1U << 2,
    TELOS_MODEL_CAPABILITY_VISION = 1U << 3,
};

struct telos_model_descriptor {
    const char *provider;
    const char *id;
    const char *name;
    enum telos_model_api api;
    uint32_t capabilities;
    size_t context_window;
    size_t maximum_output_tokens;
};

struct telos_model_catalog {
    struct telos_model_descriptor models[TELOS_MODEL_CATALOG_CAPACITY];
    size_t count;
    size_t current;
    bool has_current;
};

typedef bool (*telos_model_visit_fn)(
    const struct telos_model_descriptor *model, void *context,
    struct telos_error **error);

void telos_model_catalog_initialize(struct telos_model_catalog *catalog);

bool telos_model_catalog_add(struct telos_model_catalog *catalog,
                              const struct telos_model_descriptor *model,
                              struct telos_error **error);

const struct telos_model_descriptor *
telos_model_catalog_at(const struct telos_model_catalog *catalog, size_t index);

const struct telos_model_descriptor *telos_model_catalog_current(
    const struct telos_model_catalog *catalog);

const struct telos_model_descriptor *telos_model_catalog_find(
    const struct telos_model_catalog *catalog, const char *provider,
    const char *id);

bool telos_model_catalog_select(struct telos_model_catalog *catalog,
                                const char *provider, const char *id,
                                struct telos_error **error);

bool telos_model_catalog_select_spec(struct telos_model_catalog *catalog,
                                     const char *spec,
                                     struct telos_error **error);

bool telos_model_catalog_visit(const struct telos_model_catalog *catalog,
                               telos_model_visit_fn visit, void *context,
                               struct telos_error **error);

#endif
