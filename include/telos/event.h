#ifndef TELOS_EVENT_H
#define TELOS_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include <telos/error.h>
#include <telos/id.h>
#include <telos/value.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_event;

struct telos_event_spec {
    uint64_t sequence;
    struct telos_id event_id;
    struct telos_id session_id;
    struct telos_id correlation_id;
    struct telos_id causation_id;
    const char *type;
    const char *source;
    int64_t timestamp_milliseconds;
    const struct telos_value *payload;
};

struct telos_event *telos_event_create(
    const struct telos_event_spec *spec,
    struct telos_error **error
);

struct telos_event *telos_event_retain(const struct telos_event *event);

void telos_event_release(const struct telos_event *event);

uint64_t telos_event_sequence(const struct telos_event *event);

struct telos_id telos_event_id(const struct telos_event *event);

struct telos_id telos_event_session_id(const struct telos_event *event);

struct telos_id telos_event_correlation_id(const struct telos_event *event);

struct telos_id telos_event_causation_id(const struct telos_event *event);

const char *telos_event_type(const struct telos_event *event);

const char *telos_event_source(const struct telos_event *event);

int64_t telos_event_timestamp_milliseconds(const struct telos_event *event);

const struct telos_value *telos_event_payload(const struct telos_event *event);

#ifdef __cplusplus
}
#endif

#endif
