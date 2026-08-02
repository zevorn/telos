#ifndef TELOS_TOOLS_CHAT_H
#define TELOS_TOOLS_CHAT_H

#include <telos/config.h>
#include <telos/error.h>
#include <telos/types.h>

bool telos_chat_run(const struct telos_config *config,
                    const char *home_directory,
                    const char *current_directory,
                    const char *initial_prompt,
                    bool single_turn,
                    bool json_output,
                    bool rpc_mode,
                    bool continue_session,
                    struct telos_error **error);

#endif
