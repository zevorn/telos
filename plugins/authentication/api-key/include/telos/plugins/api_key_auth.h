#ifndef TELOS_PLUGINS_API_KEY_AUTH_H
#define TELOS_PLUGINS_API_KEY_AUTH_H

#include <telos/authentication.h>

#define TELOS_API_KEY_AUTH_ID "dev.zevorn.api-key-auth"

const struct telos_authentication_definition_v1 *
telos_deepseek_api_key_authentication_definition(void);

const struct telos_authentication_definition_v1 *
telos_zai_api_key_authentication_definition(void);

const struct telos_authentication_definition_v1 *
telos_anthropic_api_key_authentication_definition(void);

#endif
