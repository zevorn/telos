#ifndef TELOS_PLUGINS_OPENAI_CODEX_AUTH_H
#define TELOS_PLUGINS_OPENAI_CODEX_AUTH_H

#include <telos/authentication.h>

#define TELOS_OPENAI_CODEX_AUTH_ID "dev.zevorn.openai-codex-auth"
#define TELOS_OPENAI_CODEX_AUTH_ENDPOINT "https://auth.openai.com"
#define TELOS_OPENAI_CODEX_RESPONSES_ENDPOINT \
    "https://chatgpt.com/backend-api/codex"

const struct telos_authentication_definition_v1 *
telos_openai_codex_authentication_definition(void);

#endif
