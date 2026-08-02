#ifndef TELOS_COMMAND_H
#define TELOS_COMMAND_H

#include <telos/cancel.h>
#include <telos/error.h>
#include <telos/frontend.h>
#include <telos/types.h>

#define TELOS_COMMAND_REGISTRY_CAPACITY 32U
#define TELOS_COMMAND_NAME_SIZE 64U
#define TELOS_COMMAND_ARGUMENT_SIZE 4096U

typedef bool (*telos_command_run_fn)(const char *arguments,
                                     const struct telos_cancel *cancel,
                                     telos_frontend_emit_fn emit,
                                     void *emit_context, void *context,
                                     struct telos_error **error);

struct telos_command {
    const char *name;
    const char *help;
    telos_command_run_fn run;
    void *context;
};

struct telos_command_registry {
    struct telos_command commands[TELOS_COMMAND_REGISTRY_CAPACITY];
    size_t count;
};

typedef struct telos_command_registry telos_command_registry;

void telos_command_registry_initialize(struct telos_command_registry *registry);

bool telos_command_registry_add(struct telos_command_registry *registry,
                                const struct telos_command *command,
                                struct telos_error **error);

bool telos_command_registry_dispatch(const telos_command_registry *registry,
                                     const char *input,
                                     const struct telos_cancel *cancel,
                                     telos_frontend_emit_fn emit,
                                     void *emit_context, bool *handled,
                                     bool *exit_requested,
                                     struct telos_error **error);

const struct telos_command *
telos_command_registry_find(const struct telos_command_registry *registry,
                            const char *name);

#endif
