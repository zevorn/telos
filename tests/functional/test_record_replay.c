/*
 * Record & replay test: event sequences recorded by the JSONL store
 * must replay deterministically through the session state machine.
 *
 * 1. A complete recorded session replays to COMPLETED.
 * 2. A session interrupted mid-tool (process crash) is recovered by
 *    replaying the log into a fresh machine: state and last sequence
 *    are rebuilt, the next event is accepted, and re-applying an
 *    already consumed event is rejected as stale.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <telos/plugins/jsonl_store.h>
#include <telos/session.h>
#include <telos/store.h>

#define COMPLETE_FIXTURE "tests/fixtures/events/session_complete.jsonl"
#define COMPLETE_EVENTS 13U
#define INTERRUPTED_FIXTURE "tests/fixtures/events/session_interrupted.jsonl"
#define INTERRUPTED_EVENTS 7U

static int replay_all(struct telos_event_store *store,
                      struct telos_session_machine *machine)
{
    struct telos_error *error = NULL;
    size_t count = telos_event_store_count(store);
    size_t index;

    for (index = 0; index < count; ++index) {
        struct telos_event *event = telos_event_store_get(store, index, &error);

        if (event == NULL) {
            fprintf(stderr, "fixture event %zu missing\n", index);
            return 1;
        }
        if (!telos_session_machine_apply(machine, event, &error)) {
            fprintf(stderr, "replay failed at sequence %llu: %s\n",
                    (unsigned long long)telos_event_sequence(event),
                    error == NULL ? "no error" : telos_error_message(error));
            telos_event_release(event);
            return 1;
        }
        telos_event_release(event);
    }
    return 0;
}

static struct telos_event *
make_event(uint64_t sequence, const char *type, int64_t timestamp,
           struct telos_error **error)
{
    struct telos_value *payload = telos_value_new_object(NULL, NULL, 0);
    struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = {0, sequence},
        .session_id = {0, 0x0a},
        .correlation_id = {0, 0x0b},
        .causation_id = {0, sequence - 1},
        .type = type,
        .source = "frontend",
        .timestamp_milliseconds = timestamp,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, error);

    telos_value_release(payload);
    return event;
}

static int run_complete_replay(void)
{
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    struct telos_session_machine *machine;
    int failures = 0;

    store = telos_jsonl_store_create(COMPLETE_FIXTURE, &error);
    if (store == NULL) {
        fprintf(stderr, "complete fixture could not be loaded: %s\n",
                error == NULL ? "no error" : telos_error_message(error));
        return 1;
    }
    if (telos_event_store_count(store) != COMPLETE_EVENTS) {
        fprintf(stderr, "complete fixture count %zu, expected %u\n",
                telos_event_store_count(store), COMPLETE_EVENTS);
        telos_event_store_destroy(store);
        return 1;
    }

    machine = telos_session_machine_create(&error);
    if (machine == NULL) {
        telos_event_store_destroy(store);
        return 1;
    }
    if (replay_all(store, machine) != 0) {
        failures += 1;
    }
    if (telos_session_machine_state(machine) != TELOS_SESSION_COMPLETED) {
        fputs("complete replay did not reach COMPLETED\n", stderr);
        failures += 1;
    }

    telos_session_machine_destroy(machine);
    telos_event_store_destroy(store);
    return failures;
}

static int run_interrupted_recovery(void)
{
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    struct telos_session_machine *machine;
    struct telos_event *event;
    int failures = 0;

    store = telos_jsonl_store_create(INTERRUPTED_FIXTURE, &error);
    if (store == NULL) {
        fprintf(stderr, "interrupted fixture could not be loaded: %s\n",
                error == NULL ? "no error" : telos_error_message(error));
        return 1;
    }
    if (telos_event_store_count(store) != INTERRUPTED_EVENTS) {
        fprintf(stderr, "interrupted fixture count %zu, expected %u\n",
                telos_event_store_count(store), INTERRUPTED_EVENTS);
        telos_event_store_destroy(store);
        return 1;
    }

    /* Restart: a fresh machine is rebuilt by replaying the log. */
    machine = telos_session_machine_create(&error);
    if (machine == NULL) {
        telos_event_store_destroy(store);
        return 1;
    }
    if (replay_all(store, machine) != 0) {
        failures += 1;
    } else if (telos_session_machine_state(machine) !=
               TELOS_SESSION_TOOL_EXECUTE) {
        fputs("recovery replay did not rebuild TOOL_EXECUTE\n", stderr);
        failures += 1;
    }

    /* The session resumes from the recovered last sequence. */
    event = make_event(INTERRUPTED_EVENTS + 1, "tool.completed", 3000, &error);
    if (event == NULL) {
        fprintf(stderr, "resume event could not be created: %s\n",
                error == NULL ? "no error" : telos_error_message(error));
        failures += 1;
    } else if (!telos_session_machine_apply(machine, event, &error)) {
        fprintf(stderr, "resume event rejected: %s\n",
                error == NULL ? "no error" : telos_error_message(error));
        failures += 1;
    }
    telos_event_release(event);

    /* Re-applying the just-consumed event is stale, not a duplicate. */
    event = make_event(INTERRUPTED_EVENTS + 1, "tool.completed", 3001, &error);
    if (event == NULL) {
        failures += 1;
    } else if (telos_session_machine_apply(machine, event, &error)) {
        fputs("stale event was accepted\n", stderr);
        failures += 1;
    } else if (error == NULL ||
               telos_error_domain(error) != TELOS_ERROR_DOMAIN_STATE) {
        fputs("stale event rejected with the wrong error\n", stderr);
        failures += 1;
    }
    telos_event_release(event);

    /* An event before the recovered tail is stale as well. */
    error = NULL;
    event = make_event(INTERRUPTED_EVENTS, "tool.completed", 3002, &error);
    if (event == NULL) {
        failures += 1;
    } else if (telos_session_machine_apply(machine, event, &error)) {
        fputs("pre-tail event was accepted\n", stderr);
        failures += 1;
    }
    telos_event_release(event);

    telos_session_machine_destroy(machine);
    telos_event_store_destroy(store);
    return failures;
}

int main(void)
{
    int failures = run_complete_replay();

    failures += run_interrupted_recovery();

    if (failures != 0) {
        fprintf(stderr, "record-replay: %d failure(s)\n", failures);
        return 1;
    }
    printf("record-replay: replay and recovery verified\n");
    return 0;
}
