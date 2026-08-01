#ifndef TELOS_ACTOR_H
#define TELOS_ACTOR_H

#include <telos/types.h>

#include <telos/session.h>
#include <telos/store.h>

struct telos_session_actor;

typedef void (*telos_session_observer_fn)(const struct telos_event *event,
                                          enum telos_session_state state,
                                          void *context);

struct telos_session_actor_spec {
    struct telos_event_store *store;
    telos_session_observer_fn observer;
    void *observer_context;
};

struct telos_session_actor *
telos_session_actor_create(const struct telos_session_actor_spec *spec,
                           struct telos_error **error);

void telos_session_actor_destroy(struct telos_session_actor *actor);

bool telos_session_actor_submit(struct telos_session_actor *actor,
                                const struct telos_event *event,
                                struct telos_error **error);

bool telos_session_actor_wait_idle(struct telos_session_actor *actor,
                                   struct telos_error **error);

enum telos_session_state
telos_session_actor_state(struct telos_session_actor *actor);

#endif
