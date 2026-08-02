#ifndef TELOS_AUTHENTICATION_H
#define TELOS_AUTHENTICATION_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/error.h>
#include <telos/transport.h>

struct telos_authentication;
struct telos_authentication_config;
struct telos_authentication_event;
struct telos_authentication_status;

typedef struct telos_authentication telos_authentication;
typedef struct telos_authentication_config telos_authentication_config;
typedef struct telos_authentication_event telos_authentication_event;
typedef struct telos_authentication_status telos_authentication_status;

enum telos_authentication_state {
    TELOS_AUTHENTICATION_SIGNED_OUT = 1,
    TELOS_AUTHENTICATION_AUTHORIZING,
    TELOS_AUTHENTICATION_SIGNED_IN,
};

enum telos_authentication_event_kind {
    TELOS_AUTHENTICATION_VERIFICATION_REQUIRED = 1,
    TELOS_AUTHENTICATION_COMPLETED,
};

struct telos_authentication_event {
    enum telos_authentication_event_kind kind;
    const char *verification_uri;
    const char *user_code;
};

typedef bool
(*telos_authentication_event_fn)(const telos_authentication_event *event,
                                 void *context,
                                 struct telos_error **error);

struct telos_authentication_status {
    enum telos_authentication_state state;
    const char *provider;
    const char *account_id;
};

struct telos_authentication_config {
    const char *state_directory;
    const char *service_endpoint;
    telos_transport_send_fn send;
    void *transport_context;
};

typedef telos_authentication *
(*telos_authentication_create_fn)(const telos_authentication_config *config,
                                  struct telos_error **error);

typedef void
(*telos_authentication_destroy_fn)(telos_authentication *authentication);

typedef bool
(*telos_authentication_login_fn)(telos_authentication *authentication,
                                 const struct telos_cancel *cancel,
                                 telos_authentication_event_fn emit,
                                 void *emit_context,
                                 struct telos_error **error);

typedef bool
(*telos_authentication_logout_fn)(telos_authentication *authentication,
                                  struct telos_error **error);

typedef bool
(*telos_authentication_status_fn)(const telos_authentication *authentication,
                                  telos_authentication_status *status,
                                  struct telos_error **error);

typedef char *
(*telos_authentication_resolve_fn)(telos_authentication *authentication,
                                   const char *target,
                                   struct telos_error **error);

struct telos_authentication_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_authentication_create_fn create;
    telos_authentication_destroy_fn destroy;
    telos_authentication_login_fn login;
    telos_authentication_logout_fn logout;
    telos_authentication_status_fn status;
    telos_authentication_resolve_fn resolve;
};

#endif
