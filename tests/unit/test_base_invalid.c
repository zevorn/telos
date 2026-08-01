#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <telos/cancel.h>
#include <telos/clock.h>
#include <telos/error.h>
#include <telos/event.h>
#include <telos/id.h>
#include <telos/trace.h>
#include <telos/value.h>

static bool
failing_clock(void *context, int64_t *milliseconds, struct telos_error **error)
{
    (void)context;
    (void)milliseconds;
    *error = telos_error_create(TELOS_ERROR_DOMAIN_IO, EIO,
                                "fixture clock failure", NULL);
    return false;
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

int main(void)
{
    struct telos_error *error = NULL;
    struct telos_error *cause;
    struct telos_error *outer;
    struct telos_cancel *cancel;
    struct telos_clock clock = {.now = failing_clock};
    int64_t now;
    struct telos_id parsed;
    char id_text[TELOS_ID_TEXT_SIZE];
    struct telos_value *null_value = telos_value_new_null();
    struct telos_value *boolean = telos_value_new_boolean(false);
    struct telos_value *integer = telos_value_new_integer(1);
    struct telos_value *real = telos_value_new_real(1.5);
    struct telos_value *string = telos_value_new_string("one");
    struct telos_value *sensitive = telos_value_new_sensitive("secret");
    const struct telos_value *bad_items[] = {integer, NULL};
    const char *bad_keys[] = {"key", NULL};
    const struct telos_value *bad_values[] = {integer, integer};
    bool boolean_result;
    int64_t integer_result;
    double real_result;
    char buffer[8];
    struct telos_event_spec spec = {
        .sequence = 1,
        .event_id = {.high = 1, .low = 2},
        .session_id = {.high = 3, .low = 4},
        .correlation_id = {.high = 5, .low = 6},
        .causation_id = {.high = 7, .low = 8},
        .type = "fixture",
        .source = "test:base-invalid",
        .payload = null_value,
    };
    struct telos_event *event;
    struct telos_value *other;

    assert(telos_error_create(0, 1, "bad", NULL) == NULL);
    assert(telos_error_create(TELOS_ERROR_DOMAIN_PLUGIN + 1, 1, "bad", NULL) ==
           NULL);
    assert(telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, 1, NULL, NULL) ==
           NULL);
    cause = telos_error_create(TELOS_ERROR_DOMAIN_IO, EIO, "cause", NULL);
    outer =
        telos_error_create(TELOS_ERROR_DOMAIN_STATE, EINVAL, "outer", cause);
    assert(telos_error_cause(outer) == cause);
    telos_error_release(cause);
    telos_error_release(outer);
    assert(telos_error_retain(NULL) == NULL);
    telos_error_release(NULL);
    assert(telos_error_domain(NULL) == 0);
    assert(telos_error_code(NULL) == 0);
    assert(strcmp(telos_error_message(NULL), "") == 0);
    assert(telos_error_cause(NULL) == NULL);

    assert(telos_cancel_retain(NULL) == NULL);
    telos_cancel_release(NULL);
    assert(!telos_cancel_request(NULL));
    assert(!telos_cancel_requested(NULL));
    cancel = telos_cancel_create();
    assert(telos_cancel_retain(cancel) == cancel);
    assert(telos_cancel_request(cancel));
    assert(!telos_cancel_request(cancel));
    telos_cancel_release(cancel);
    telos_cancel_release(cancel);

    assert(!telos_clock_now_milliseconds(NULL, &now, &error));
    clear_error(&error);
    assert(!telos_clock_now_milliseconds(&clock, NULL, &error));
    clear_error(&error);
    assert(!telos_clock_now_milliseconds(&clock, &now, &error));
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_IO);
    clear_error(&error);

    assert(!telos_id_equal((struct telos_id){1, 2}, (struct telos_id){1, 3}));
    assert(!telos_id_format((struct telos_id){0}, NULL, 0));
    assert(!telos_id_format((struct telos_id){0}, id_text,
                            TELOS_ID_TEXT_SIZE - 1));
    assert(telos_id_parse("ABCDEF0123456789abcdef0123456789", &parsed));
    assert(!telos_id_parse(NULL, &parsed));
    assert(!telos_id_parse("", &parsed));
    assert(!telos_id_parse("gggggggggggggggg0000000000000000", &parsed));
    assert(!telos_id_parse("0000000000000000gggggggggggggggg", &parsed));
    assert(!telos_id_parse("00000000000000000000000000000000", NULL));

    assert(telos_value_new_string(NULL) == NULL);
    assert(telos_value_new_sensitive(NULL) == NULL);
    assert(telos_value_new_array(NULL, 1) == NULL);
    assert(telos_value_new_array(NULL, SIZE_MAX) == NULL);
    assert(telos_value_new_array(bad_items, 2) == NULL);
    assert(telos_value_new_object(NULL, NULL, 1) == NULL);
    assert(telos_value_new_object(bad_keys, bad_values, 2) == NULL);
    assert(telos_value_new_object(NULL, NULL, SIZE_MAX) == NULL);
    assert(telos_value_retain(NULL) == NULL);
    telos_value_release(NULL);
    assert(telos_value_type(NULL) == 0);
    assert(!telos_value_boolean(NULL, &boolean_result));
    assert(!telos_value_boolean(boolean, NULL));
    assert(!telos_value_boolean(integer, &boolean_result));
    assert(!telos_value_integer(NULL, &integer_result));
    assert(!telos_value_integer(integer, NULL));
    assert(!telos_value_integer(real, &integer_result));
    assert(!telos_value_real(NULL, &real_result));
    assert(!telos_value_real(real, NULL));
    assert(!telos_value_real(integer, &real_result));
    assert(telos_value_string(NULL) == NULL);
    assert(telos_value_string(integer) == NULL);
    assert(telos_value_count(NULL) == 0);
    assert(telos_value_count(integer) == 0);
    assert(telos_value_at(NULL, 0) == NULL);
    assert(telos_value_at(integer, 0) == NULL);
    assert(telos_value_key_at(NULL, 0) == NULL);
    assert(telos_value_key_at(integer, 0) == NULL);
    assert(telos_value_get(NULL, "key") == NULL);
    assert(telos_value_get(integer, "key") == NULL);
    assert(telos_value_get(null_value, NULL) == NULL);
    assert(!telos_value_equal(NULL, integer));
    assert(!telos_value_equal(integer, real));
    other = telos_value_new_integer(2);
    assert(!telos_value_equal(integer, other));
    telos_value_release(other);
    other = telos_value_new_real(2.5);
    assert(!telos_value_equal(real, other));
    telos_value_release(other);
    other = telos_value_new_boolean(true);
    assert(!telos_value_equal(boolean, other));
    telos_value_release(other);
    other = telos_value_new_string("two");
    assert(!telos_value_equal(string, other));
    telos_value_release(other);
    other = telos_value_new_sensitive("other");
    assert(!telos_value_equal(sensitive, other));
    telos_value_release(other);
    assert(telos_value_json_size(NULL) == 0);
    assert(!telos_value_write_json(NULL, buffer, sizeof(buffer), NULL, &error));
    clear_error(&error);
    assert(!telos_value_write_json(integer, NULL, 0, NULL, &error));
    clear_error(&error);
    assert(!telos_value_write_json(integer, buffer, 1, NULL, &error));
    clear_error(&error);

    assert(telos_event_create(NULL, &error) == NULL);
    clear_error(&error);
    spec.sequence = 0;
    assert(telos_event_create(&spec, &error) == NULL);
    clear_error(&error);
    spec.sequence = 1;
    spec.type = "";
    assert(telos_event_create(&spec, &error) == NULL);
    clear_error(&error);
    spec.type = "fixture";
    spec.source = "";
    assert(telos_event_create(&spec, &error) == NULL);
    clear_error(&error);
    spec.source = "test:base-invalid";
    spec.payload = NULL;
    assert(telos_event_create(&spec, &error) == NULL);
    clear_error(&error);
    spec.payload = null_value;
    event = telos_event_create(&spec, &error);
    assert(event != NULL);
    assert(telos_event_retain(event) == event);
    telos_event_release(event);
    telos_event_release(event);
    assert(telos_event_retain(NULL) == NULL);
    telos_event_release(NULL);
    assert(telos_event_sequence(NULL) == 0);
    assert(telos_event_id(NULL).high == 0);
    assert(telos_event_session_id(NULL).high == 0);
    assert(telos_event_correlation_id(NULL).high == 0);
    assert(telos_event_causation_id(NULL).high == 0);
    assert(telos_event_type(NULL) == NULL);
    assert(telos_event_source(NULL) == NULL);
    assert(telos_event_timestamp_milliseconds(NULL) == 0);
    assert(telos_event_payload(NULL) == NULL);
    assert(telos_event_trace_json_size(NULL) == 0);
    assert(!telos_event_write_trace_json(NULL, buffer, sizeof(buffer), NULL,
                                         &error));
    clear_error(&error);

    telos_value_release(sensitive);
    telos_value_release(string);
    telos_value_release(real);
    telos_value_release(integer);
    telos_value_release(boolean);
    telos_value_release(null_value);
    return 0;
}
