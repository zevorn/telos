#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include <telos/actor.h>
#include <telos/plugins/memory_store.h>

struct rendezvous {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned int arrivals;
    bool concurrent;
};

static struct telos_event *new_event(uint64_t sequence, const char *type)
{
    struct telos_value *payload = telos_value_new_null();
    const struct telos_event_spec spec = {
        .sequence = sequence,
        .event_id = telos_id_generate(),
        .session_id = telos_id_generate(),
        .correlation_id = telos_id_generate(),
        .causation_id = telos_id_generate(),
        .type = type,
        .source = "test",
        .timestamp_milliseconds = (int64_t)sequence,
        .payload = payload,
    };
    struct telos_event *event = telos_event_create(&spec, NULL);

    telos_value_release(payload);
    return event;
}

static void observe(const struct telos_event *event,
                    enum telos_session_state state,
                    void *context)
{
    struct rendezvous *rendezvous = context;
    struct timespec deadline;

    (void)event;
    (void)state;
    timespec_get(&deadline, TIME_UTC);
    deadline.tv_sec += 2;

    pthread_mutex_lock(&rendezvous->mutex);
    rendezvous->arrivals += 1;
    pthread_cond_broadcast(&rendezvous->condition);
    while (rendezvous->arrivals < 2) {
        if (pthread_cond_timedwait(&rendezvous->condition, &rendezvous->mutex,
                                   &deadline) != 0) {
            break;
        }
    }
    if (rendezvous->arrivals == 2) {
        rendezvous->concurrent = true;
    }
    pthread_mutex_unlock(&rendezvous->mutex);
}

int main(void)
{
    struct rendezvous rendezvous = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER,
    };
    struct telos_event_store *first_store = telos_memory_store_create(NULL);
    struct telos_event_store *second_store = telos_memory_store_create(NULL);
    const struct telos_session_actor_spec first_spec = {
        .store = first_store,
        .observer = observe,
        .observer_context = &rendezvous,
    };
    const struct telos_session_actor_spec second_spec = {
        .store = second_store,
        .observer = observe,
        .observer_context = &rendezvous,
    };
    struct telos_session_actor *first =
        telos_session_actor_create(&first_spec, NULL);
    struct telos_session_actor *second =
        telos_session_actor_create(&second_spec, NULL);
    struct telos_event *first_event = new_event(1, "turn.accepted");
    struct telos_event *second_event = new_event(1, "turn.accepted");
    bool passed;

    passed = first != NULL && second != NULL &&
             telos_session_actor_submit(first, first_event, NULL) &&
             telos_session_actor_submit(second, second_event, NULL) &&
             telos_session_actor_wait_idle(first, NULL) &&
             telos_session_actor_wait_idle(second, NULL) &&
             rendezvous.concurrent &&
             telos_session_actor_state(first) == TELOS_SESSION_TURN_ACCEPTED &&
             telos_session_actor_state(second) == TELOS_SESSION_TURN_ACCEPTED &&
             telos_event_store_count(first_store) == 1 &&
             telos_event_store_count(second_store) == 1;

    if (passed) {
        const char *remaining[] = {
            "input.prepare",     "context.build", "provider.dispatch",
            "response.received", "final.commit",  "final.committed",
        };

        for (size_t index = 0; index < sizeof(remaining) / sizeof(remaining[0]);
             ++index) {
            struct telos_event *event = new_event(index + 2, remaining[index]);

            passed =
                event != NULL && telos_session_actor_submit(first, event, NULL);
            telos_event_release(event);
            if (!passed) {
                break;
            }
        }
        passed = passed && telos_session_actor_wait_idle(first, NULL) &&
                 telos_session_actor_state(first) == TELOS_SESSION_COMPLETED &&
                 telos_event_store_count(first_store) == 7;
    }

    telos_event_release(second_event);
    telos_event_release(first_event);
    telos_session_actor_destroy(second);
    telos_session_actor_destroy(first);
    telos_event_store_destroy(second_store);
    telos_event_store_destroy(first_store);
    pthread_cond_destroy(&rendezvous.condition);
    pthread_mutex_destroy(&rendezvous.mutex);

    if (!passed) {
        fputs("Session Actors did not process independently and serially\n",
              stderr);
        return 1;
    }
    return 0;
}
