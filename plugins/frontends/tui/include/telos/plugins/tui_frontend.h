#ifndef TELOS_PLUGINS_TUI_FRONTEND_H
#define TELOS_PLUGINS_TUI_FRONTEND_H

#include <telos/frontend.h>
#include <telos/tui_plugin.h>
#include <telos/types.h>

#define TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES (16U * 1024U)
#define TELOS_TUI_MAXIMUM_PLUGINS 16U

struct telos_tui_frontend_config {
    const struct telos_frontend_session *session;
    int input_descriptor;
    int output_descriptor;
    size_t maximum_input_bytes;
    bool force_plain;
    bool json_output;
    bool rpc_mode;
    const struct telos_tui_plugin_definition_v1 *tui_plugins;
    size_t tui_plugin_count;
};

typedef struct telos_tui_frontend_config telos_tui_frontend_config;

bool telos_tui_frontend_run(const telos_tui_frontend_config *config,
                                 struct telos_error **error);

bool telos_tui_frontend_run_stdio(const telos_frontend_session *session,
                                       struct telos_error **error);

#endif
