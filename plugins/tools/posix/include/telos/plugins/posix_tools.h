#ifndef TELOS_PLUGINS_POSIX_TOOLS_H
#define TELOS_PLUGINS_POSIX_TOOLS_H

#include <telos/registry.h>
#include <telos/types.h>
#include <telos/value.h>

struct telos_posix_tools;

struct telos_posix_tools_config {
    const char *working_directory;
    const char *shell;
};

struct telos_posix_tools *
telos_posix_tools_create(const struct telos_posix_tools_config *config,
                         struct telos_error **error);

void telos_posix_tools_destroy(struct telos_posix_tools *tools);

bool telos_posix_tools_register(struct telos_posix_tools *tools,
                                struct telos_registry *registry,
                                struct telos_error **error);

struct telos_value *
telos_posix_tools_describe(struct telos_error **error);

#endif
