#ifndef TELOS_PLUGIN_PROCESS_H
#define TELOS_PLUGIN_PROCESS_H

#include <stdbool.h>

#include <telos/cancel.h>
#include <telos/error.h>
#include <telos/value.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_plugin_process;

struct telos_plugin_process *telos_plugin_process_spawn(
    const char *host_path,
    struct telos_error **error
);

struct telos_plugin_process *telos_plugin_process_spawn_plugin(
    const char *host_path,
    const char *plugin_path,
    const char *plugin_id,
    struct telos_error **error
);

bool telos_plugin_process_execute_tool(
    struct telos_plugin_process *process,
    const char *tool_id,
    const struct telos_value *arguments,
    unsigned int timeout_milliseconds,
    const struct telos_cancel *cancel,
    struct telos_value **response_body,
    struct telos_error **error
);

bool telos_plugin_process_request(
    struct telos_plugin_process *process,
    const char *type,
    const struct telos_value *body,
    unsigned int timeout_milliseconds,
    const struct telos_cancel *cancel,
    struct telos_value **response_body,
    struct telos_error **error
);

bool telos_plugin_process_shutdown(
    struct telos_plugin_process *process,
    unsigned int timeout_milliseconds,
    struct telos_error **error
);

void telos_plugin_process_destroy(struct telos_plugin_process *process);

#ifdef __cplusplus
}
#endif

#endif
