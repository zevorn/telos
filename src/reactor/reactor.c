#include <errno.h>
#include <limits.h>
#include <string.h>

#include <telos/reactor.h>
/* No-heap path: heap calls below this point fail to compile. */
#define TELOS_NO_HEAP 1
#include <telos/no_heap.h>

#define REACTOR_HANDLE_INDEX_BITS 8U
#define REACTOR_HANDLE_INDEX_MASK \
    ((UINT32_C(1) << REACTOR_HANDLE_INDEX_BITS) - 1U)

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain, int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_static(domain, code, message);
    }
}

static bool now_milliseconds(const struct telos_reactor *reactor,
                             uint64_t *milliseconds,
                             struct telos_error **error)
{
    int64_t current;

    if (!telos_clock_now_milliseconds(&reactor->clock, &current, error) ||
        current < 0) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Reactor clock returned an invalid time");
        }
        return false;
    }
    *milliseconds = (uint64_t)current;
    return true;
}

static telos_reactor_handle make_handle(size_t index, uint32_t generation)
{
    return (telos_reactor_handle)((generation << REACTOR_HANDLE_INDEX_BITS) |
                                  (uint32_t)(index + 1U));
}

static bool decode_handle(telos_reactor_handle handle, size_t *index,
                          uint32_t *generation)
{
    uint32_t encoded_index = handle & REACTOR_HANDLE_INDEX_MASK;

    if (encoded_index == 0U || encoded_index > TELOS_REACTOR_CAPACITY ||
        index == NULL || generation == NULL) {
        return false;
    }
    *index = (size_t)(encoded_index - 1U);
    *generation = handle >> REACTOR_HANDLE_INDEX_BITS;
    return *generation != 0U;
}

static bool slot_ready(const struct telos_reactor_slot *slot,
                       uint64_t now)
{
    return slot->used && slot->due_milliseconds <= now;
}

static size_t active_slot_count(const telos_reactor *reactor)
{
    size_t count = 0;

    for (size_t index = 0; index < TELOS_REACTOR_CAPACITY; ++index) {
        if (reactor->slots[index].used) {
            ++count;
        }
    }
    return count;
}

static size_t next_slot(const telos_reactor *reactor, uint64_t now,
                        bool ready)
{
    size_t selected = TELOS_REACTOR_CAPACITY;
    uint64_t selected_due = UINT64_MAX;
    uint64_t selected_sequence = UINT64_MAX;

    for (size_t index = 0; index < TELOS_REACTOR_CAPACITY; ++index) {
        const struct telos_reactor_slot *slot = &reactor->slots[index];

        if (!slot->used || (ready && !slot_ready(slot, now)) ||
            (!ready && slot_ready(slot, now))) {
            continue;
        }
        if (slot->due_milliseconds < selected_due ||
            (slot->due_milliseconds == selected_due &&
             slot->sequence < selected_sequence)) {
            selected = index;
            selected_due = slot->due_milliseconds;
            selected_sequence = slot->sequence;
        }
    }
    return selected;
}

bool telos_reactor_initialize(telos_reactor *reactor,
                              const telos_reactor_options *options,
                              struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (reactor == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Reactor storage is required");
        return false;
    }
    memset(reactor, 0, sizeof(*reactor));
    reactor->clock = options == NULL || options->clock.now == NULL
                         ? telos_system_clock()
                         : options->clock;
    reactor->wait = options == NULL ? NULL : options->wait;
    reactor->wait_context = options == NULL ? NULL : options->wait_context;
    return true;
}

void telos_reactor_stop(telos_reactor *reactor)
{
    if (reactor != NULL) {
        reactor->stopping = true;
    }
}

bool telos_reactor_is_stopping(const telos_reactor *reactor)
{
    return reactor != NULL && reactor->stopping;
}

bool telos_reactor_schedule(telos_reactor *reactor,
                            uint64_t delay_milliseconds,
                            telos_reactor_task_fn task, void *context,
                            telos_reactor_handle *handle,
                            struct telos_error **error)
{
    uint64_t now;
    size_t index = TELOS_REACTOR_CAPACITY;
    struct telos_reactor_slot *slot;

    if (error != NULL) {
        *error = NULL;
    }
    if (handle != NULL) {
        *handle = 0;
    }
    if (reactor == NULL || task == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Reactor task arguments are invalid");
        return false;
    }
    if (reactor->stopping) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ECANCELED,
                  "Reactor is stopping");
        return false;
    }
    if (!now_milliseconds(reactor, &now, error) ||
        delay_milliseconds > UINT64_MAX - now) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EOVERFLOW,
                      "Reactor task deadline overflow");
        }
        return false;
    }
    for (size_t candidate = 0; candidate < TELOS_REACTOR_CAPACITY;
         ++candidate) {
        if (!reactor->slots[candidate].used) {
            index = candidate;
            break;
        }
    }
    if (index == TELOS_REACTOR_CAPACITY) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOSPC,
                  "Reactor task queue is full");
        return false;
    }
    slot = &reactor->slots[index];
    slot->generation += 1U;
    if (slot->generation == 0U) {
        slot->generation = 1U;
    }
    slot->task = task;
    slot->context = context;
    slot->due_milliseconds = now + delay_milliseconds;
    slot->sequence = ++reactor->next_sequence;
    slot->used = true;
    if (handle != NULL) {
        *handle = make_handle(index, slot->generation);
    }
    return true;
}

bool telos_reactor_post(telos_reactor *reactor, telos_reactor_task_fn task,
                        void *context, telos_reactor_handle *handle,
                        struct telos_error **error)
{
    return telos_reactor_schedule(reactor, 0, task, context, handle, error);
}

bool telos_reactor_cancel(telos_reactor *reactor,
                          telos_reactor_handle handle,
                          struct telos_error **error)
{
    size_t index;
    uint32_t generation;
    struct telos_reactor_slot *slot;

    if (error != NULL) {
        *error = NULL;
    }
    if (reactor == NULL || !decode_handle(handle, &index, &generation)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Reactor task handle is invalid");
        return false;
    }
    slot = &reactor->slots[index];
    if (!slot->used || slot->generation != generation) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOENT,
                  "Reactor task is no longer pending");
        return false;
    }
    slot->used = false;
    slot->task = NULL;
    slot->context = NULL;
    return true;
}

bool telos_reactor_run_once(telos_reactor *reactor,
                            const struct telos_cancel *cancel,
                            bool *executed, struct telos_error **error)
{
    uint64_t now;
    size_t index;
    telos_reactor_task_fn task;
    void *context;

    if (error != NULL) {
        *error = NULL;
    }
    if (executed != NULL) {
        *executed = false;
    }
    if (reactor == NULL || executed == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Reactor step arguments are invalid");
        return false;
    }
    if (reactor->stopping) {
        return true;
    }
    if (telos_cancel_requested(cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "Reactor run was cancelled");
        return false;
    }
    if (!now_milliseconds(reactor, &now, error)) {
        return false;
    }
    index = next_slot(reactor, now, true);
    if (index == TELOS_REACTOR_CAPACITY) {
        return true;
    }
    task = reactor->slots[index].task;
    context = reactor->slots[index].context;
    reactor->slots[index].used = false;
    reactor->slots[index].task = NULL;
    reactor->slots[index].context = NULL;
    *executed = true;
    return task(context, cancel, error);
}

bool telos_reactor_run(telos_reactor *reactor,
                       const struct telos_cancel *cancel,
                       struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (reactor == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Reactor is required");
        return false;
    }
    while (!reactor->stopping && active_slot_count(reactor) > 0) {
        uint64_t now;
        size_t index;
        bool executed;

        if (!telos_reactor_run_once(reactor, cancel, &executed, error)) {
            return false;
        }
        if (executed) {
            continue;
        }
        if (!now_milliseconds(reactor, &now, error)) {
            return false;
        }
        index = next_slot(reactor, now, false);
        if (index == TELOS_REACTOR_CAPACITY || reactor->wait == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_STATE, EAGAIN,
                      "Reactor has delayed work but no wait hook");
            return false;
        }
        if (!reactor->wait(reactor->slots[index].due_milliseconds - now,
                           reactor->wait_context, error)) {
            return false;
        }
    }
    return true;
}

size_t telos_reactor_pending(const telos_reactor *reactor)
{
    return reactor == NULL ? 0 : active_slot_count(reactor);
}
