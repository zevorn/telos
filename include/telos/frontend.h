#ifndef TELOS_FRONTEND_H
#define TELOS_FRONTEND_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/error.h>

struct telos_command_registry;

enum telos_frontend_event_kind {
    TELOS_FRONTEND_RESPONSE_STARTED = 1,
    TELOS_FRONTEND_TEXT_DELTA,
    TELOS_FRONTEND_TOOL_STARTED,
    TELOS_FRONTEND_TOOL_COMPLETED,
    TELOS_FRONTEND_TOOL_FAILED,
    TELOS_FRONTEND_NOTICE,
    TELOS_FRONTEND_CLIPBOARD,
};

struct telos_frontend_event {
    enum telos_frontend_event_kind kind;
    const char *text;
    const char *name;
};

typedef bool (*telos_frontend_emit_fn)(const struct telos_frontend_event *event,
                                       void *context,
                                       struct telos_error **error);

typedef bool (*telos_frontend_turn_fn)(const char *input,
                                       const struct telos_cancel *cancel,
                                       telos_frontend_emit_fn emit,
                                       void *emit_context,
                                       void *turn_context,
                                       struct telos_error **error);

struct telos_frontend_session {
    const char *application;
    const char *version;
    const char *provider;
    const char *model;
    const char *working_directory;
    const char *command_help;
    const char *initial_prompt;
    const struct telos_command_registry *commands;
    const char *(*provider_get)(void *context);
    const char *(*model_get)(void *context);
    void *identity_context;
    bool single_turn;
    telos_frontend_turn_fn turn;
    void *turn_context;
};

typedef struct telos_frontend_session telos_frontend_session;

struct telos_frontend_definition_v1 {
    uint32_t struct_size;
    const char *id;
    bool (*run)(const struct telos_frontend_session *session,
                struct telos_error **error);
};

#endif
