#ifndef TELOS_PLUGINS_TERMINAL_FRONTEND_H
#define TELOS_PLUGINS_TERMINAL_FRONTEND_H

#include <telos/frontend.h>
#include <telos/types.h>

#define TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES (16U * 1024U)

struct telos_terminal_frontend_config {
    const struct telos_frontend_session *session;
    int input_descriptor;
    int output_descriptor;
    size_t maximum_input_bytes;
    bool force_plain;
};

typedef struct telos_terminal_frontend_config telos_terminal_frontend_config;

bool telos_terminal_frontend_run(const telos_terminal_frontend_config *config,
                                 struct telos_error **error);

bool telos_terminal_frontend_run_stdio(const telos_frontend_session *session,
                                       struct telos_error **error);

#endif
