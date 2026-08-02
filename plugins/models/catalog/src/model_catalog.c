#include <errno.h>

#include <telos/plugins/model_catalog.h>

static const struct telos_model_descriptor models[] = {
    {
        .provider = "openai",
        .id = "gpt-5",
        .name = "GPT-5",
        .api = TELOS_MODEL_API_OPENAI_RESPONSES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_REASONING,
    },
    {
        .provider = "openai",
        .id = "gpt-5-mini",
        .name = "GPT-5 Mini",
        .api = TELOS_MODEL_API_OPENAI_RESPONSES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS,
    },
    {
        .provider = "openai",
        .id = "gpt-4.1",
        .name = "GPT-4.1",
        .api = TELOS_MODEL_API_OPENAI_RESPONSES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS,
    },
    {
        .provider = "deepseek",
        .id = "deepseek-chat",
        .name = "DeepSeek Chat",
        .api = TELOS_MODEL_API_OPENAI_CHAT,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS,
    },
    {
        .provider = "deepseek",
        .id = "deepseek-reasoner",
        .name = "DeepSeek Reasoner",
        .api = TELOS_MODEL_API_OPENAI_CHAT,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_REASONING,
    },
    {
        .provider = "zai",
        .id = "glm-4.7",
        .name = "GLM-4.7",
        .api = TELOS_MODEL_API_OPENAI_CHAT,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_REASONING,
    },
    {
        .provider = "zai",
        .id = "glm-4.5-air",
        .name = "GLM-4.5 Air",
        .api = TELOS_MODEL_API_OPENAI_CHAT,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_REASONING,
    },
    {
        .provider = "anthropic",
        .id = "claude-sonnet-4-5",
        .name = "Claude Sonnet",
        .api = TELOS_MODEL_API_ANTHROPIC_MESSAGES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_VISION,
    },
    {
        .provider = "anthropic",
        .id = "claude-opus-4-1",
        .name = "Claude Opus",
        .api = TELOS_MODEL_API_ANTHROPIC_MESSAGES,
        .capabilities = TELOS_MODEL_CAPABILITY_STREAMING |
                        TELOS_MODEL_CAPABILITY_TOOLS |
                        TELOS_MODEL_CAPABILITY_REASONING |
                        TELOS_MODEL_CAPABILITY_VISION,
    },
};

bool telos_official_model_catalog_add(struct telos_model_catalog *catalog,
                                      struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (catalog == NULL) {
        if (error != NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                                         "Model catalog is required", NULL);
        }
        return false;
    }
    for (size_t index = 0; index < sizeof(models) / sizeof(models[0]);
         ++index) {
        if (!telos_model_catalog_add(catalog, &models[index], error)) {
            return false;
        }
    }
    return true;
}
