/*
 * Soak test: long-running stability probe for the Telos core.
 *
 * Each round exercises the lifecycle that a production session
 * follows: open a Task Scope, register and commit/dispose it, run
 * a reactor cycle, append and replay session events through the
 * state machine.  The process runs for --rounds rounds (default:
 * forever) and reports a heartbeat with peak RSS every 1000 rounds,
 * so a wrapper script can watch for memory growth, hangs or leaks.
 *
 * Exits non-zero on any API failure.  Built with
 * -Db_sanitize=address the LeakSanitizer reports leaks on exit.
 */
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>

#include <telos/plugins/memory_store.h>
#include <telos/reactor.h>
#include <telos/session.h>
#include <telos/store.h>
#include <telos/task_scope.h>

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static uint64_t now_milliseconds(void)
{
    struct timespec current;

    clock_gettime(CLOCK_MONOTONIC, &current);
    return (uint64_t)current.tv_sec * 1000U +
           (uint64_t)current.tv_nsec / 1000000U;
}

static long peak_rss_kilobytes(void)
{
    struct rusage usage;

    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return -1;
    }
#if defined(__APPLE__)
    return usage.ru_maxrss / 1024; /* bytes on macOS */
#else
    return usage.ru_maxrss; /* kilobytes on Linux */
#endif
}

static bool noop_task(void *context, const struct telos_cancel *cancel,
                      struct telos_error **error)
{
    (void)context;
    (void)cancel;
    (void)error;
    return true;
}

static void noop_dispose(void *context)
{
    (void)context;
}

static bool run_round(uint64_t round, uint64_t *sequence,
                      struct telos_error **error)
{
    struct telos_task_scope *scope;
    struct telos_session_machine *machine;
    struct telos_event_store *store;
    struct telos_reactor reactor;
    telos_reactor_handle handle;
    struct telos_value *payload;
    struct telos_event *event;
    struct telos_event_spec spec;
    bool executed = false;

    scope = telos_task_scope_open(NULL, "soak", error);
    if (scope == NULL) {
        return false;
    }
    if (!telos_task_scope_register(scope, "probe", noop_dispose, NULL,
                                   error)) {
        telos_task_scope_dispose(scope);
        return false;
    }

    machine = telos_session_machine_create(error);
    store = telos_memory_store_create(error);
    if (machine == NULL || store == NULL ||
        !telos_reactor_initialize(&reactor, NULL, error)) {
        telos_session_machine_destroy(machine);
        if (store != NULL) {
            telos_event_store_destroy(store);
        }
        telos_task_scope_dispose(scope);
        return false;
    }
    if (!telos_reactor_post(&reactor, noop_task, NULL, &handle, error) ||
        !telos_reactor_run_once(&reactor, NULL, &executed, error) ||
        !executed) {
        telos_event_store_destroy(store);
        telos_session_machine_destroy(machine);
        telos_task_scope_dispose(scope);
        return false;
    }

    payload = telos_value_new_object(NULL, NULL, 0);
    if (payload == NULL) {
        telos_event_store_destroy(store);
        telos_session_machine_destroy(machine);
        telos_task_scope_dispose(scope);
        return false;
    }
    *sequence += 1;
    spec.sequence = *sequence;
    spec.event_id = (struct telos_id){0, *sequence};
    spec.session_id = (struct telos_id){0, 0x0a};
    spec.correlation_id = (struct telos_id){0, 0x0b};
    spec.causation_id = (struct telos_id){0, *sequence - 1};
    spec.type = "turn.accepted";
    spec.source = "soak";
    spec.timestamp_milliseconds = (int64_t)round;
    spec.payload = payload;
    event = telos_event_create(&spec, error);
    if (event == NULL ||
        !telos_event_store_append(store, event, error) ||
        !telos_session_machine_apply(machine, event, error)) {
        telos_event_release(event);
        telos_value_release(payload);
        telos_event_store_destroy(store);
        telos_session_machine_destroy(machine);
        telos_task_scope_dispose(scope);
        return false;
    }
    telos_event_release(event);
    telos_value_release(payload);

    if (round % 2 == 0) {
        telos_task_scope_dispose(scope);
    } else {
        if (!telos_task_scope_commit(scope, error)) {
            telos_task_scope_destroy(scope);
            telos_event_store_destroy(store);
            telos_session_machine_destroy(machine);
            return false;
        }
    }
    telos_event_store_destroy(store);
    telos_session_machine_destroy(machine);
    return true;
}

int main(int argc, char **argv)
{
    uint64_t rounds = UINT64_MAX;
    uint64_t interval_milliseconds = 0;
    uint64_t round = 0;
    uint64_t sequence = 0;
    uint64_t started;
    struct telos_error *error = NULL;
    long initial_rss = -1;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--rounds") == 0 && index + 1 < argc) {
            rounds = strtoull(argv[++index], NULL, 10);
        } else if (strcmp(argv[index], "--interval-ms") == 0 &&
                   index + 1 < argc) {
            interval_milliseconds = strtoull(argv[++index], NULL, 10);
        } else {
            fprintf(stderr, "usage: %s [--rounds N] [--interval-ms N]\n",
                    argv[0]);
            return 2;
        }
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    started = now_milliseconds();

    while (round < rounds && !stop_requested) {
        if (!run_round(round + 1, &sequence, &error)) {
            fprintf(stderr, "soak round %" PRIu64 " failed: %s\n", round + 1,
                    error == NULL ? "no error" : telos_error_message(error));
            return 1;
        }
        round += 1;
        if (interval_milliseconds > 0) {
            struct timespec pause = {
                .tv_sec = (time_t)(interval_milliseconds / 1000),
                .tv_nsec = (long)(interval_milliseconds % 1000) * 1000000L,
            };

            nanosleep(&pause, NULL);
        }
        if (round % 1000 == 0) {
            long rss = peak_rss_kilobytes();
            uint64_t elapsed = now_milliseconds() - started;

            if (initial_rss < 0) {
                initial_rss = rss;
            }
            printf("soak round %" PRIu64 " events %" PRIu64
                   " elapsed %" PRIu64 "ms peak_rss %ldKB\n",
                   round, sequence, elapsed, rss);
            fflush(stdout);
        }
    }

    printf("soak completed: %" PRIu64 " rounds in %" PRIu64 "ms\n", round,
           now_milliseconds() - started);
    if (initial_rss > 0) {
        printf("soak peak_rss %ldKB\n", peak_rss_kilobytes());
    }
    return 0;
}
