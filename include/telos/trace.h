#ifndef TELOS_TRACE_H
#define TELOS_TRACE_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/error.h>
#include <telos/event.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t telos_event_trace_json_size(const struct telos_event *event);

bool telos_event_write_trace_json(
    const struct telos_event *event,
    char *buffer,
    size_t buffer_size,
    size_t *written,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
