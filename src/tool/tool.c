#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/tool.h>

struct telos_capability_broker {
    size_t capability_count;
    char **capabilities;
    telos_policy_evaluate_fn evaluate;
    void *evaluate_context;
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

static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

struct telos_capability_broker *
telos_capability_broker_create(const char *const *available_capabilities,
                               size_t available_capability_count,
                               telos_policy_evaluate_fn evaluate,
                               void *evaluate_context,
                               struct telos_error **error)
{
    struct telos_capability_broker *broker;

    if (error != NULL) {
        *error = NULL;
    }
    if ((available_capability_count > 0 && available_capabilities == NULL) ||
        evaluate == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Capability Broker configuration is invalid");
        return NULL;
    }
    if (available_capability_count > SIZE_MAX / sizeof(*broker->capabilities)) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Capability Broker size overflow");
        return NULL;
    }
    for (size_t index = 0; index < available_capability_count; ++index) {
        if (available_capabilities[index] == NULL ||
            available_capabilities[index][0] == '\0') {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                      "Capability names must not be empty");
            return NULL;
        }
    }
    broker = calloc(1, sizeof(*broker));
    if (broker == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Capability Broker allocation failed");
        return NULL;
    }
    if (available_capability_count > 0) {
        broker->capabilities =
            calloc(available_capability_count, sizeof(*broker->capabilities));
        if (broker->capabilities == NULL) {
            free(broker);
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "Capability Broker allocation failed");
            return NULL;
        }
        for (size_t index = 0; index < available_capability_count; ++index) {
            broker->capabilities[index] =
                copy_string(available_capabilities[index]);
            if (broker->capabilities[index] == NULL) {
                broker->capability_count = index;
                telos_capability_broker_destroy(broker);
                set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                          "Capability name allocation failed");
                return NULL;
            }
            broker->capability_count = index + 1;
        }
    }
    broker->evaluate = evaluate;
    broker->evaluate_context = evaluate_context;
    return broker;
}

void telos_capability_broker_destroy(struct telos_capability_broker *broker)
{
    if (broker == NULL) {
        return;
    }
    for (size_t index = 0; index < broker->capability_count; ++index) {
        free(broker->capabilities[index]);
    }
    free(broker->capabilities);
    free(broker);
}

static bool capability_available(const struct telos_capability_broker *broker,
                                 const char *capability)
{
    for (size_t index = 0; index < broker->capability_count; ++index) {
        if (strcmp(broker->capabilities[index], capability) == 0) {
            return true;
        }
    }
    return false;
}

static bool value_matches_type(const struct telos_value *value,
                               const char *type)
{
    if (strcmp(type, "object") == 0) {
        return telos_value_type(value) == TELOS_VALUE_OBJECT;
    }
    if (strcmp(type, "array") == 0) {
        return telos_value_type(value) == TELOS_VALUE_ARRAY;
    }
    if (strcmp(type, "string") == 0) {
        return telos_value_type(value) == TELOS_VALUE_STRING;
    }
    if (strcmp(type, "integer") == 0) {
        return telos_value_type(value) == TELOS_VALUE_INTEGER;
    }
    if (strcmp(type, "number") == 0) {
        return telos_value_type(value) == TELOS_VALUE_INTEGER ||
               telos_value_type(value) == TELOS_VALUE_REAL;
    }
    if (strcmp(type, "boolean") == 0) {
        return telos_value_type(value) == TELOS_VALUE_BOOLEAN;
    }
    if (strcmp(type, "null") == 0) {
        return telos_value_type(value) == TELOS_VALUE_NULL;
    }
    return false;
}

static bool validate_schema(const struct telos_value *schema,
                            const struct telos_value *value,
                            unsigned int depth)
{
    const char *type;

    if (schema == NULL || telos_value_type(schema) != TELOS_VALUE_OBJECT ||
        value == NULL || depth > 32) {
        return false;
    }
    type = telos_value_string(telos_value_get(schema, "type"));
    if (type == NULL) {
        return true;
    }
    if (!value_matches_type(value, type)) {
        return false;
    }
    if (strcmp(type, "object") == 0) {
        const struct telos_value *required =
            telos_value_get(schema, "required");
        const struct telos_value *properties =
            telos_value_get(schema, "properties");

        if (required != NULL &&
            telos_value_type(required) != TELOS_VALUE_ARRAY) {
            return false;
        }
        for (size_t index = 0; index < telos_value_count(required); ++index) {
            const char *name =
                telos_value_string(telos_value_at(required, index));

            if (name == NULL || telos_value_get(value, name) == NULL) {
                return false;
            }
        }
        if (properties != NULL) {
            if (telos_value_type(properties) != TELOS_VALUE_OBJECT) {
                return false;
            }
            for (size_t index = 0; index < telos_value_count(properties);
                 ++index) {
                const char *name = telos_value_key_at(properties, index);
                const struct telos_value *member = telos_value_get(value, name);

                if (member != NULL &&
                    !validate_schema(telos_value_get(properties, name), member,
                                     depth + 1)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool telos_tool_execute(const struct telos_registry_generation *generation,
                        struct telos_capability_broker *broker,
                        enum telos_execution_domain domain,
                        const char *tool_id,
                        const struct telos_value *arguments,
                        const struct telos_cancel *cancel,
                        struct telos_value **result,
                        struct telos_error **error)
{
    const struct telos_extension_descriptor *descriptor;
    const struct telos_tool_definition *tool;
    struct telos_policy_request policy_request;
    enum telos_policy_decision decision;
    struct telos_tool_context context = {
        .cancel = cancel,
        .context = NULL,
    };

    if (error != NULL) {
        *error = NULL;
    }
    if (result != NULL) {
        *result = NULL;
    }
    if (generation == NULL || broker == NULL || tool_id == NULL ||
        tool_id[0] == '\0' || arguments == NULL || result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool execution arguments are invalid");
        return false;
    }
    if (domain != TELOS_EXECUTION_CORE) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Providers cannot invoke local Tools directly");
        return false;
    }
    if (telos_cancel_requested(cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "Tool execution was cancelled");
        return false;
    }

    descriptor = telos_registry_generation_find(generation,
                                                TELOS_EXTENSION_TOOL, tool_id);
    if (descriptor == NULL || descriptor->implementation == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                  "Tool is not present in the Registry Generation");
        return false;
    }
    tool = descriptor->implementation;
    if (tool->id == NULL || strcmp(tool->id, tool_id) != 0 ||
        tool->input_schema == NULL || tool->execute == NULL ||
        !validate_schema(tool->input_schema, arguments, 0)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Tool arguments do not match the input schema");
        return false;
    }
    context.context = tool->context;
    for (size_t index = 0; index < tool->required_capability_count; ++index) {
        if (tool->required_capabilities == NULL ||
            tool->required_capabilities[index] == NULL ||
            !capability_available(broker, tool->required_capabilities[index])) {
            set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EACCES,
                      "Tool requires an unavailable Capability");
            return false;
        }
    }

    policy_request.tool_id = tool_id;
    policy_request.arguments = arguments;
    policy_request.required_capabilities = tool->required_capabilities;
    policy_request.required_capability_count = tool->required_capability_count;
    decision = broker->evaluate(&policy_request, broker->evaluate_context);
    if (decision != TELOS_POLICY_ALLOW) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION,
                  decision == TELOS_POLICY_REQUIRE_APPROVAL ? EAGAIN : EACCES,
                  decision == TELOS_POLICY_REQUIRE_APPROVAL
                      ? "Tool execution requires explicit approval"
                      : "Tool execution was denied by Policy");
        return false;
    }

    if (!tool->execute(&context, arguments, result, error)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_STATE, EIO,
                      "Tool execution failed");
        }
        telos_value_release(*result);
        *result = NULL;
        return false;
    }
    if (*result == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, EPROTO,
                  "Tool reported success without a result");
        return false;
    }
    if (telos_cancel_requested(cancel)) {
        telos_value_release(*result);
        *result = NULL;
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "Tool completed after cancellation");
        return false;
    }
    return true;
}
