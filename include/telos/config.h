#ifndef TELOS_CONFIG_H
#define TELOS_CONFIG_H

#include <stdbool.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_config_origin {
    TELOS_CONFIG_DEFAULT = 1,
    TELOS_CONFIG_USER,
    TELOS_CONFIG_PROJECT,
    TELOS_CONFIG_ENVIRONMENT,
    TELOS_CONFIG_COMMAND_LINE,
};

struct telos_config;

struct telos_config *telos_config_load(
    const char *home_directory,
    const char *project_directory,
    struct telos_error **error
);

void telos_config_destroy(struct telos_config *config);

bool telos_config_override(
    struct telos_config *config,
    const char *key,
    const char *value,
    struct telos_error **error
);

const char *telos_config_get(
    const struct telos_config *config,
    const char *key
);

enum telos_config_origin telos_config_get_origin(
    const struct telos_config *config,
    const char *key
);

#ifdef __cplusplus
}
#endif

#endif
