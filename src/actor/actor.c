#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include <telos/actor.h>

struct actor_event {
    struct telos_event *event;
    struct actor_event *next;
};

struct telos_session_actor {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_cond_t idle;
    pthread_t thread;
    bool thread_started;
    bool stopping;
    bool active;
    struct actor_event *head;
    struct actor_event *tail;
    struct telos_session_machine *machine;
    struct telos_event_store *store;
    telos_session_observer_fn observer;
    void *observer_context;
    struct telos_error *failure;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message,
                      const struct telos_error *cause)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, cause);
    }
}

static void remember_failure(struct telos_session_actor *actor,
                             const struct telos_error *error)
{
    pthread_mutex_lock(&actor->mutex);
    if (actor->failure == NULL) {
        actor->failure = telos_error_retain(error);
    }
    pthread_mutex_unlock(&actor->mutex);
}

static void *actor_worker(void *context)
{
    struct telos_session_actor *actor = context;

    for (;;) {
        struct actor_event *queued;
        struct telos_error *error = NULL;
        bool applied;
        enum telos_session_state state;

        pthread_mutex_lock(&actor->mutex);
        while (actor->head == NULL && !actor->stopping) {
            pthread_cond_wait(&actor->wake, &actor->mutex);
        }
        if (actor->head == NULL && actor->stopping) {
            pthread_mutex_unlock(&actor->mutex);
            break;
        }

        queued = actor->head;
        actor->head = queued->next;
        if (actor->head == NULL) {
            actor->tail = NULL;
        }
        actor->active = true;
        pthread_mutex_unlock(&actor->mutex);

        pthread_mutex_lock(&actor->mutex);
        applied =
            telos_session_machine_apply(actor->machine, queued->event, &error);
        state = telos_session_machine_state(actor->machine);
        pthread_mutex_unlock(&actor->mutex);
        if (applied && actor->store != NULL &&
            !telos_event_store_append(actor->store, queued->event, &error)) {
            applied = false;
        }
        if (applied && actor->observer != NULL) {
            actor->observer(queued->event, state, actor->observer_context);
        } else if (!applied) {
            remember_failure(actor, error);
        }

        telos_error_release(error);
        telos_event_release(queued->event);
        free(queued);

        pthread_mutex_lock(&actor->mutex);
        actor->active = false;
        if (actor->head == NULL) {
            pthread_cond_broadcast(&actor->idle);
        }
        pthread_mutex_unlock(&actor->mutex);
    }
    return NULL;
}

struct telos_session_actor *
telos_session_actor_create(const struct telos_session_actor_spec *spec,
                           struct telos_error **error)
{
    struct telos_session_actor *actor;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    actor = calloc(1, sizeof(*actor));
    if (actor == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session Actor allocation failed", NULL);
        return NULL;
    }

    result = pthread_mutex_init(&actor->mutex, NULL);
    if (result != 0) {
        free(actor);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Session Actor mutex initialization failed", NULL);
        return NULL;
    }
    result = pthread_cond_init(&actor->wake, NULL);
    if (result != 0) {
        pthread_mutex_destroy(&actor->mutex);
        free(actor);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Session Actor condition initialization failed", NULL);
        return NULL;
    }
    result = pthread_cond_init(&actor->idle, NULL);
    if (result != 0) {
        pthread_cond_destroy(&actor->wake);
        pthread_mutex_destroy(&actor->mutex);
        free(actor);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Session Actor idle condition initialization failed", NULL);
        return NULL;
    }

    actor->machine = telos_session_machine_create(error);
    if (actor->machine == NULL) {
        pthread_cond_destroy(&actor->idle);
        pthread_cond_destroy(&actor->wake);
        pthread_mutex_destroy(&actor->mutex);
        free(actor);
        return NULL;
    }
    if (spec != NULL) {
        actor->store = spec->store;
        actor->observer = spec->observer;
        actor->observer_context = spec->observer_context;
    }

    result = pthread_create(&actor->thread, NULL, actor_worker, actor);
    if (result != 0) {
        telos_session_machine_destroy(actor->machine);
        pthread_cond_destroy(&actor->idle);
        pthread_cond_destroy(&actor->wake);
        pthread_mutex_destroy(&actor->mutex);
        free(actor);
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Session Actor worker creation failed", NULL);
        return NULL;
    }
    actor->thread_started = true;
    return actor;
}

void telos_session_actor_destroy(struct telos_session_actor *actor)
{
    struct actor_event *queued;

    if (actor == NULL) {
        return;
    }
    pthread_mutex_lock(&actor->mutex);
    actor->stopping = true;
    pthread_cond_broadcast(&actor->wake);
    pthread_mutex_unlock(&actor->mutex);
    if (actor->thread_started) {
        pthread_join(actor->thread, NULL);
    }

    queued = actor->head;
    while (queued != NULL) {
        struct actor_event *next = queued->next;

        telos_event_release(queued->event);
        free(queued);
        queued = next;
    }
    telos_error_release(actor->failure);
    telos_session_machine_destroy(actor->machine);
    pthread_cond_destroy(&actor->idle);
    pthread_cond_destroy(&actor->wake);
    pthread_mutex_destroy(&actor->mutex);
    free(actor);
}

bool telos_session_actor_submit(struct telos_session_actor *actor,
                                const struct telos_event *event,
                                struct telos_error **error)
{
    struct actor_event *queued;

    if (error != NULL) {
        *error = NULL;
    }
    if (actor == NULL || event == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session Actor submission arguments are invalid", NULL);
        return false;
    }

    queued = calloc(1, sizeof(*queued));
    if (queued == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Session Actor queue allocation failed", NULL);
        return false;
    }
    queued->event = telos_event_retain(event);

    pthread_mutex_lock(&actor->mutex);
    if (actor->stopping || actor->failure != NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE,
                  actor->stopping ? ECANCELED : EIO,
                  actor->stopping ? "Session Actor is stopping"
                                  : "Session Actor has failed",
                  actor->failure);
        pthread_mutex_unlock(&actor->mutex);
        telos_event_release(queued->event);
        free(queued);
        return false;
    }
    if (actor->tail == NULL) {
        actor->head = queued;
    } else {
        actor->tail->next = queued;
    }
    actor->tail = queued;
    pthread_cond_signal(&actor->wake);
    pthread_mutex_unlock(&actor->mutex);
    return true;
}

bool telos_session_actor_wait_idle(struct telos_session_actor *actor,
                                   struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (actor == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Session Actor is required", NULL);
        return false;
    }

    pthread_mutex_lock(&actor->mutex);
    while (actor->head != NULL || actor->active) {
        pthread_cond_wait(&actor->idle, &actor->mutex);
    }
    if (actor->failure != NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EIO,
                  "Session Actor event processing failed", actor->failure);
        pthread_mutex_unlock(&actor->mutex);
        return false;
    }
    pthread_mutex_unlock(&actor->mutex);
    return true;
}

enum telos_session_state
telos_session_actor_state(struct telos_session_actor *actor)
{
    enum telos_session_state state;

    if (actor == NULL) {
        return 0;
    }
    pthread_mutex_lock(&actor->mutex);
    state = telos_session_machine_state(actor->machine);
    pthread_mutex_unlock(&actor->mutex);
    return state;
}
