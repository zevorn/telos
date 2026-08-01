#include <errno.h>
#include <string.h>

#include <telos/state_fragment.h>

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

bool telos_state_fragment_validate(const struct telos_state_fragment *fragment,
                                   struct telos_error **error)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (fragment == NULL || fragment->id == NULL || fragment->id[0] == '\0' ||
        fragment->slot < TELOS_SLOT_INPUT_PREPARE ||
        fragment->slot > TELOS_SLOT_FINAL_COMMIT ||
        fragment->accepted_event_types == NULL ||
        fragment->accepted_event_type_count == 0 || fragment->handle == NULL ||
        fragment->timeout_milliseconds == 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "State Fragment descriptor is invalid");
        return false;
    }
    for (size_t index = 0; index < fragment->accepted_event_type_count;
         ++index) {
        if (fragment->accepted_event_types[index] == NULL ||
            fragment->accepted_event_types[index][0] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "State Fragment accepted Event is invalid");
            return false;
        }
    }
    return true;
}

bool telos_state_fragment_execute(const telos_state_fragment *fragment,
                                  enum telos_extension_slot slot,
                                  const telos_state_fragment_context *context,
                                  const struct telos_event *event,
                                  enum telos_fragment_result *result,
                                  struct telos_error **error)
{
    bool accepted = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (!telos_state_fragment_validate(fragment, error) || context == NULL ||
        event == NULL || result == NULL || slot != fragment->slot) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                      "State Fragment cannot run in this Extension Slot");
        }
        return false;
    }
    if (telos_cancel_requested(context->cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "State Fragment execution was cancelled");
        return false;
    }
    for (size_t index = 0; index < fragment->accepted_event_type_count;
         ++index) {
        if (strcmp(fragment->accepted_event_types[index],
                   telos_event_type(event)) == 0) {
            accepted = true;
            break;
        }
    }
    if (!accepted) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOMSG,
                  "State Fragment does not accept this Event");
        return false;
    }
    *result = fragment->handle(context, event, error);
    if (*result < TELOS_FRAGMENT_COMPLETED ||
        *result > TELOS_FRAGMENT_FATAL_ERROR) {
        set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EPROTO,
                  "State Fragment returned an invalid result");
        return false;
    }
    return true;
}
