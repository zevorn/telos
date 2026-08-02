#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include <telos/reactor.h>

struct fake_clock {
    int64_t milliseconds;
    size_t waits;
};

struct task_context {
    unsigned int *order;
    unsigned int value;
};

static bool fake_now(void *context, int64_t *milliseconds,
                     struct telos_error **error)
{
    struct fake_clock *clock = context;

    (void)error;
    *milliseconds = clock->milliseconds;
    return true;
}

static bool fake_wait(uint64_t milliseconds, void *context,
                      struct telos_error **error)
{
    struct fake_clock *clock = context;

    (void)error;
    clock->milliseconds += (int64_t)milliseconds;
    ++clock->waits;
    return true;
}

static bool record_task(void *context, const struct telos_cancel *cancel,
                        struct telos_error **error)
{
    struct task_context *task = context;

    (void)cancel;
    (void)error;
    *task->order = *task->order * 10U + task->value;
    return true;
}

static bool failing_task(void *context, const struct telos_cancel *cancel,
                         struct telos_error **error)
{
    (void)context;
    (void)cancel;
    *error = telos_error_create(TELOS_ERROR_DOMAIN_STATE, EIO,
                                "task failed", NULL);
    return false;
}

int main(void)
{
    struct fake_clock clock = {.milliseconds = 100};
    const struct telos_reactor_options options = {
        .clock = {
            .context = &clock,
            .now = fake_now,
        },
        .wait = fake_wait,
        .wait_context = &clock,
    };
    struct telos_reactor reactor;
    struct telos_error *error = NULL;
    struct task_context first = {0};
    struct task_context second = {0};
    unsigned int order = 0;
    telos_reactor_handle handle = 0;
    bool executed = false;

    assert(telos_reactor_initialize(&reactor, &options, &error));
    assert(error == NULL);
    first.order = &order;
    first.value = 1;
    second.order = &order;
    second.value = 2;
    assert(telos_reactor_post(&reactor, record_task, &first, &handle,
                              &error));
    assert(handle != 0 && error == NULL);
    assert(telos_reactor_schedule(&reactor, 20, record_task, &second, NULL,
                                  &error));
    assert(telos_reactor_pending(&reactor) == 2);
    assert(telos_reactor_run_once(&reactor, NULL, &executed, &error));
    assert(executed && order == 1);
    assert(telos_reactor_pending(&reactor) == 1);
    assert(telos_reactor_cancel(&reactor, handle, &error) == false);
    assert(error != NULL && telos_error_code(error) == ENOENT);
    telos_error_release(error);
    error = NULL;
    assert(telos_reactor_run(&reactor, NULL, &error));
    assert(order == 12 && clock.waits == 1);
    assert(telos_reactor_pending(&reactor) == 0);

    assert(telos_reactor_post(&reactor, failing_task, NULL, NULL, &error));
    assert(!telos_reactor_run(&reactor, NULL, &error));
    assert(error != NULL && telos_error_code(error) == EIO);
    telos_error_release(error);
    error = NULL;
    assert(telos_reactor_pending(&reactor) == 0);

    telos_reactor_stop(&reactor);
    assert(telos_reactor_is_stopping(&reactor));
    assert(!telos_reactor_post(&reactor, record_task, &first, NULL, &error));
    assert(error != NULL && telos_error_code(error) == ECANCELED);
    telos_error_release(error);
    return 0;
}
