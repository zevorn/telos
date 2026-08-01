#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <telos/event.h>

struct telos_event {
    atomic_uint references;
    uint64_t sequence;
    struct telos_id event_id;
    struct telos_id session_id;
    struct telos_id correlation_id;
    struct telos_id causation_id;
    char *type;
    char *source;
    int64_t timestamp_milliseconds;
    struct telos_value *payload;
};

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_text(const char *text)
{
    char *copy;
    const size_t size = strlen(text) + 1;

    copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }

    return copy;
}

struct telos_event *telos_event_create(const struct telos_event_spec *spec,
                                       struct telos_error **error)
{
    struct telos_event *event;

    if (error != NULL) {
        *error = NULL;
    }

    if (spec == NULL || spec->sequence == 0 || spec->type == NULL ||
        spec->type[0] == '\0' || spec->source == NULL ||
        spec->source[0] == '\0' || spec->payload == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "event specification is invalid");
        return NULL;
    }

    event = calloc(1, sizeof(*event));
    if (event == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "event allocation failed");
        return NULL;
    }

    event->type = copy_text(spec->type);
    event->source = copy_text(spec->source);
    if (event->type == NULL || event->source == NULL) {
        free(event->source);
        free(event->type);
        free(event);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "event metadata allocation failed");
        return NULL;
    }

    atomic_init(&event->references, 1);
    event->sequence = spec->sequence;
    event->event_id = spec->event_id;
    event->session_id = spec->session_id;
    event->correlation_id = spec->correlation_id;
    event->causation_id = spec->causation_id;
    event->timestamp_milliseconds = spec->timestamp_milliseconds;
    event->payload = telos_value_retain(spec->payload);
    return event;
}

struct telos_event *telos_event_retain(const struct telos_event *event)
{
    struct telos_event *mutable_event = (struct telos_event *)event;

    if (mutable_event != NULL) {
        atomic_fetch_add_explicit(&mutable_event->references, 1,
                                  memory_order_relaxed);
    }

    return mutable_event;
}

void telos_event_release(const struct telos_event *event)
{
    struct telos_event *mutable_event = (struct telos_event *)event;

    if (mutable_event == NULL) {
        return;
    }

    if (atomic_fetch_sub_explicit(&mutable_event->references, 1,
                                  memory_order_acq_rel) == 1) {
        telos_value_release(mutable_event->payload);
        free(mutable_event->source);
        free(mutable_event->type);
        free(mutable_event);
    }
}

uint64_t telos_event_sequence(const struct telos_event *event)
{
    return event == NULL ? 0 : event->sequence;
}

struct telos_id telos_event_id(const struct telos_event *event)
{
    return event == NULL ? (struct telos_id){0} : event->event_id;
}

struct telos_id telos_event_session_id(const struct telos_event *event)
{
    return event == NULL ? (struct telos_id){0} : event->session_id;
}

struct telos_id telos_event_correlation_id(const struct telos_event *event)
{
    return event == NULL ? (struct telos_id){0} : event->correlation_id;
}

struct telos_id telos_event_causation_id(const struct telos_event *event)
{
    return event == NULL ? (struct telos_id){0} : event->causation_id;
}

const char *telos_event_type(const struct telos_event *event)
{
    return event == NULL ? NULL : event->type;
}

const char *telos_event_source(const struct telos_event *event)
{
    return event == NULL ? NULL : event->source;
}

int64_t telos_event_timestamp_milliseconds(const struct telos_event *event)
{
    return event == NULL ? 0 : event->timestamp_milliseconds;
}

const struct telos_value *telos_event_payload(const struct telos_event *event)
{
    return event == NULL ? NULL : event->payload;
}
