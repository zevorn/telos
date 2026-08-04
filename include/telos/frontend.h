#ifndef TELOS_FRONTEND_H
#define TELOS_FRONTEND_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/error.h>
#include <telos/model.h>

struct telos_command_registry;

#define TELOS_FRONTEND_COMPLETION_VALUE_SIZE 4096U
#define TELOS_FRONTEND_COMPLETION_LABEL_SIZE 512U
#define TELOS_FRONTEND_COMPLETION_DETAIL_SIZE 1024U

struct telos_frontend_completion_item {
    char value[TELOS_FRONTEND_COMPLETION_VALUE_SIZE];
    char label[TELOS_FRONTEND_COMPLETION_LABEL_SIZE];
    char detail[TELOS_FRONTEND_COMPLETION_DETAIL_SIZE];
};

typedef size_t (*telos_frontend_completion_count_fn)(const char *input,
                                                      void *context);

typedef bool (*telos_frontend_completion_at_fn)(
    const char *input, size_t ordinal,
    struct telos_frontend_completion_item *item, void *context);

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

typedef char *(*telos_frontend_steer_next_fn)(void *context,
                                              struct telos_error **error);

struct telos_frontend_steer {
    telos_frontend_steer_next_fn next;
    void *context;
};

typedef bool (*telos_frontend_turn_fn)(const char *input,
                                       const struct telos_cancel *cancel,
                                       telos_frontend_emit_fn emit,
                                       void *emit_context,
                                       const struct telos_frontend_steer *steer,
                                       void *turn_context,
                                       struct telos_error **error);

enum telos_frontend_status_field {
    TELOS_FRONTEND_STATUS_MODEL = 1U << 0,
    TELOS_FRONTEND_STATUS_THINKING = 1U << 1,
    TELOS_FRONTEND_STATUS_PATH = 1U << 2,
    TELOS_FRONTEND_STATUS_BRANCH = 1U << 3,
    TELOS_FRONTEND_STATUS_CONTEXT = 1U << 4,
};

struct telos_frontend_status {
    unsigned int fields;
    const char *(*thinking_get)(void *context);
    const char *(*branch_get)(void *context);
    size_t (*context_used_get)(void *context);
    size_t (*context_window_get)(void *context);
    const char *home_directory;
    void *context;
};

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
    const struct telos_model_catalog *model_catalog;
    const struct telos_frontend_status *status;
    telos_frontend_completion_count_fn completion_count;
    telos_frontend_completion_at_fn completion_at;
    void *completion_context;
};

typedef struct telos_frontend_session telos_frontend_session;

struct telos_frontend_definition_v1 {
    uint32_t struct_size;
    const char *id;
    bool (*run)(const struct telos_frontend_session *session,
                struct telos_error **error);
};

#endif
