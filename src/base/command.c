#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <telos/command.h>

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool valid_name(const char *name)
{
    size_t size;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    size = strlen(name);
    if (size >= TELOS_COMMAND_NAME_SIZE) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        if (name[index] == '/' || isspace((unsigned char)name[index])) {
            return false;
        }
    }
    return true;
}

void telos_command_registry_initialize(struct telos_command_registry *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

const struct telos_command *
telos_command_registry_find(const struct telos_command_registry *registry,
                            const char *name)
{
    if (registry == NULL || name == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < registry->count; ++index) {
        if (strcmp(registry->commands[index].name, name) == 0) {
            return &registry->commands[index];
        }
    }
    return NULL;
}

bool telos_command_registry_add(struct telos_command_registry *registry,
                                const struct telos_command *command,
                                struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (registry == NULL || command == NULL || !valid_name(command->name) ||
        command->run == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Command definition is invalid");
        return false;
    }
    if (registry->count >= TELOS_COMMAND_REGISTRY_CAPACITY) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOSPC,
                  "Command registry is full");
        return false;
    }
    if (telos_command_registry_find(registry, command->name) != NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EEXIST,
                  "Command is already registered");
        return false;
    }
    registry->commands[registry->count++] = *command;
    return true;
}

static bool parse_input(const char *input, char name[TELOS_COMMAND_NAME_SIZE],
                        char arguments[TELOS_COMMAND_ARGUMENT_SIZE],
                        bool *is_command, struct telos_error **error)
{
    const char *cursor;
    const char *name_end;
    size_t name_size;
    size_t argument_size;

    *is_command = false;
    if (input == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Command input is required");
        return false;
    }
    cursor = input;
    while (isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor != '/') {
        return true;
    }
    *is_command = true;
    ++cursor;
    name_end = cursor;
    while (*name_end != '\0' && !isspace((unsigned char)*name_end)) {
        ++name_end;
    }
    name_size = (size_t)(name_end - cursor);
    if (name_size == 0 || name_size >= TELOS_COMMAND_NAME_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Command name is invalid");
        return false;
    }
    memcpy(name, cursor, name_size);
    name[name_size] = '\0';
    while (isspace((unsigned char)*name_end)) {
        ++name_end;
    }
    argument_size = strlen(name_end);
    if (argument_size >= TELOS_COMMAND_ARGUMENT_SIZE) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Command arguments are too long");
        return false;
    }
    memcpy(arguments, name_end, argument_size + 1);
    return true;
}

bool telos_command_registry_dispatch(const telos_command_registry *registry,
                                     const char *input,
                                     const struct telos_cancel *cancel,
                                     telos_frontend_emit_fn emit,
                                     void *emit_context, bool *handled,
                                     bool *exit_requested,
                                     struct telos_error **error)
{
    char name[TELOS_COMMAND_NAME_SIZE];
    char arguments[TELOS_COMMAND_ARGUMENT_SIZE];
    const struct telos_command *command;
    bool is_command;

    if (error != NULL) {
        *error = NULL;
    }
    if (registry == NULL || emit == NULL || handled == NULL ||
        exit_requested == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Command dispatch arguments are invalid");
        return false;
    }
    *handled = false;
    *exit_requested = false;
    if (!parse_input(input, name, arguments, &is_command, error)) {
        return false;
    }
    if (!is_command) {
        return true;
    }
    command = telos_command_registry_find(registry, name);
    if (command == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                  "Command is not registered");
        return false;
    }
    if (!command->run(arguments, cancel, emit, emit_context, command->context,
                      error)) {
        return false;
    }
    *handled = true;
    *exit_requested = strcmp(name, "quit") == 0 ||
                      strcmp(name, "exit") == 0;
    return true;
}
