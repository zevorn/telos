#ifndef TELOS_REACTOR_H
#define TELOS_REACTOR_H

#include <telos/cancel.h>
#include <telos/clock.h>
#include <telos/error.h>
#include <telos/types.h>

#define TELOS_REACTOR_CAPACITY 64U

typedef uint32_t telos_reactor_handle;

typedef bool (*telos_reactor_task_fn)(void *context,
                                      const struct telos_cancel *cancel,
                                      struct telos_error **error);

typedef bool (*telos_reactor_wait_fn)(uint64_t milliseconds, void *context,
                                      struct telos_error **error);

struct telos_reactor_options {
    struct telos_clock clock;
    telos_reactor_wait_fn wait;
    void *wait_context;
};

struct telos_reactor_slot {
    telos_reactor_task_fn task;
    void *context;
    uint64_t due_milliseconds;
    uint64_t sequence;
    uint32_t generation;
    bool used;
};

struct telos_reactor {
    struct telos_clock clock;
    telos_reactor_wait_fn wait;
    void *wait_context;
    struct telos_reactor_slot slots[TELOS_REACTOR_CAPACITY];
    uint64_t next_sequence;
    bool stopping;
};

typedef struct telos_reactor telos_reactor;
typedef struct telos_reactor_options telos_reactor_options;

bool telos_reactor_initialize(telos_reactor *reactor,
                              const telos_reactor_options *options,
                              struct telos_error **error);

void telos_reactor_stop(telos_reactor *reactor);

bool telos_reactor_is_stopping(const telos_reactor *reactor);

bool telos_reactor_schedule(telos_reactor *reactor,
                            uint64_t delay_milliseconds,
                            telos_reactor_task_fn task, void *context,
                            telos_reactor_handle *handle,
                            struct telos_error **error);

bool telos_reactor_post(telos_reactor *reactor, telos_reactor_task_fn task,
                        void *context, telos_reactor_handle *handle,
                        struct telos_error **error);

bool telos_reactor_cancel(telos_reactor *reactor,
                          telos_reactor_handle handle,
                          struct telos_error **error);

bool telos_reactor_run_once(telos_reactor *reactor,
                            const struct telos_cancel *cancel,
                            bool *executed, struct telos_error **error);

bool telos_reactor_run(telos_reactor *reactor,
                       const struct telos_cancel *cancel,
                       struct telos_error **error);

size_t telos_reactor_pending(const telos_reactor *reactor);

#endif
