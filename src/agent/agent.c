#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <telos/agent.h>

struct pending_call {
    char *call_id;
    char *name;
    struct telos_value *arguments;
    struct telos_value *result;
    struct telos_error *error;
};

struct response_collector {
    char *text;
    size_t text_size;
    char *response_id;
    struct pending_call *calls;
    size_t call_count;
    size_t call_capacity;
    bool completed;
};

struct tool_worker {
    const struct telos_agent_options *options;
    const struct telos_cancel *cancel;
    struct pending_call *call;
};

static void set_error(
    struct telos_error **error,
    enum telos_error_domain domain,
    int code,
    const char *message
)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_text(const char *text)
{
    size_t size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    size = strlen(text) + 1;
    copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static void pending_call_clear(struct pending_call *call)
{
    if (call == NULL) {
        return;
    }
    telos_error_release(call->error);
    telos_value_release(call->result);
    telos_value_release(call->arguments);
    free(call->name);
    free(call->call_id);
    memset(call, 0, sizeof(*call));
}

static void collector_clear(struct response_collector *collector)
{
    if (collector == NULL) {
        return;
    }
    for (size_t index = 0; index < collector->call_count; ++index) {
        pending_call_clear(&collector->calls[index]);
    }
    free(collector->calls);
    free(collector->response_id);
    free(collector->text);
    memset(collector, 0, sizeof(*collector));
}

static bool append_text(
    struct response_collector *collector,
    const char *delta
)
{
    size_t delta_size;
    size_t next_size;
    char *next;

    if (delta == NULL) {
        return false;
    }
    delta_size = strlen(delta);
    if (delta_size > SIZE_MAX - collector->text_size - 1) {
        return false;
    }
    next_size = collector->text_size + delta_size;
    next = realloc(collector->text, next_size + 1);
    if (next == NULL) {
        return false;
    }
    memcpy(next + collector->text_size, delta, delta_size + 1);
    collector->text = next;
    collector->text_size = next_size;
    return true;
}

static bool add_call(
    struct response_collector *collector,
    const struct telos_provider_event *event
)
{
    struct pending_call *calls;
    size_t capacity;
    struct pending_call *call;

    if (
        event->call_id == NULL
        || event->call_id[0] == '\0'
        || event->name == NULL
        || event->name[0] == '\0'
        || event->payload == NULL
    ) {
        return false;
    }
    for (size_t index = 0; index < collector->call_count; ++index) {
        if (strcmp(collector->calls[index].call_id, event->call_id) == 0) {
            return false;
        }
    }
    if (collector->call_count == collector->call_capacity) {
        capacity = collector->call_capacity == 0
            ? 4
            : collector->call_capacity * 2;
        if (
            capacity < collector->call_capacity
            || capacity > SIZE_MAX / sizeof(*calls)
        ) {
            return false;
        }
        calls = realloc(collector->calls, capacity * sizeof(*calls));
        if (calls == NULL) {
            return false;
        }
        memset(
            calls + collector->call_capacity,
            0,
            (capacity - collector->call_capacity) * sizeof(*calls)
        );
        collector->calls = calls;
        collector->call_capacity = capacity;
    }
    call = &collector->calls[collector->call_count];
    call->call_id = copy_text(event->call_id);
    call->name = copy_text(event->name);
    call->arguments = telos_value_retain(event->payload);
    if (
        call->call_id == NULL
        || call->name == NULL
        || call->arguments == NULL
    ) {
        pending_call_clear(call);
        return false;
    }
    collector->call_count += 1;
    return true;
}

static bool collect_event(
    const struct telos_provider_event *event,
    void *context,
    struct telos_error **error
)
{
    struct response_collector *collector = context;

    if (event == NULL || collector == NULL || collector->completed) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "Provider emitted an invalid or late Event"
        );
        return false;
    }
    switch (event->kind) {
    case TELOS_PROVIDER_RESPONSE_STARTED:
        if (
            collector->response_id != NULL
            || event->response_id == NULL
            || event->response_id[0] == '\0'
        ) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_PROTOCOL,
                EPROTO,
                "Provider response identifier is invalid or duplicated"
            );
            return false;
        }
        collector->response_id = copy_text(event->response_id);
        if (collector->response_id != NULL) {
            return true;
        }
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Provider response identifier could not be collected"
        );
        return false;
    case TELOS_PROVIDER_TEXT_DELTA:
        if (append_text(collector, event->delta)) {
            return true;
        }
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Provider text could not be collected"
        );
        return false;
    case TELOS_PROVIDER_TOOL_CALL_COMPLETED:
        if (add_call(collector, event)) {
            return true;
        }
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EPROTO,
            "Provider Tool Call is invalid or duplicated"
        );
        return false;
    case TELOS_PROVIDER_RESPONSE_COMPLETED:
        collector->completed = true;
        return true;
    case TELOS_PROVIDER_ERROR:
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PROTOCOL,
            EIO,
            "Provider reported an error"
        );
        return false;
    default:
        return true;
    }
}

static void *execute_tool(void *context)
{
    struct tool_worker *worker = context;

    if (!telos_tool_execute(
        worker->options->registry_generation,
        worker->options->capability_broker,
        TELOS_EXECUTION_CORE,
        worker->call->name,
        worker->call->arguments,
        worker->cancel,
        &worker->call->result,
        &worker->call->error
    ) && worker->call->error == NULL) {
        worker->call->error = telos_error_create(
            TELOS_ERROR_DOMAIN_PLUGIN,
            EIO,
            "Tool failed without an error",
            NULL
        );
    }
    return NULL;
}

static bool run_tools(
    const struct telos_agent_options *options,
    const struct telos_cancel *cancel,
    struct response_collector *collector,
    struct telos_error **error
)
{
    pthread_t *threads;
    struct tool_worker *workers;
    bool result = true;

    if (collector->call_count == 0) {
        return true;
    }
    if (collector->call_count > SIZE_MAX / sizeof(*threads)) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Tool worker count overflow"
        );
        return false;
    }
    threads = calloc(collector->call_count, sizeof(*threads));
    workers = calloc(collector->call_count, sizeof(*workers));
    if (threads == NULL || workers == NULL) {
        free(workers);
        free(threads);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Tool workers could not be allocated"
        );
        return false;
    }

    size_t started = 0;
    for (; started < collector->call_count; ++started) {
        workers[started] = (struct tool_worker) {
            .options = options,
            .cancel = cancel,
            .call = &collector->calls[started],
        };
        if (
            pthread_create(
                &threads[started],
                NULL,
                execute_tool,
                &workers[started]
            ) != 0
        ) {
            break;
        }
    }
    for (size_t index = 0; index < started; ++index) {
        pthread_join(threads[index], NULL);
    }
    if (started != collector->call_count) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EAGAIN,
            "Tool worker could not be started"
        );
        result = false;
    }
    if (result) {
        for (size_t index = 0; index < collector->call_count; ++index) {
            if (collector->calls[index].error != NULL) {
                if (error != NULL) {
                    *error = telos_error_retain(
                        collector->calls[index].error
                    );
                }
                result = false;
                break;
            }
        }
    }
    free(workers);
    free(threads);
    return result;
}

static char *value_json(const struct telos_value *value)
{
    size_t size = telos_value_json_size(value);
    char *json = malloc(size);

    if (
        json == NULL
        || !telos_value_write_json(value, json, size, NULL, NULL)
    ) {
        free(json);
        return NULL;
    }
    return json;
}

static struct telos_value *function_call_item(
    const struct pending_call *call
)
{
    char *arguments_json = value_json(call->arguments);
    struct telos_value *type = telos_value_new_string("function_call");
    struct telos_value *call_id = telos_value_new_string(call->call_id);
    struct telos_value *name = telos_value_new_string(call->name);
    struct telos_value *arguments = telos_value_new_string(arguments_json);
    const char *keys[] = {"type", "call_id", "name", "arguments"};
    const struct telos_value *values[] = {
        type,
        call_id,
        name,
        arguments,
    };
    struct telos_value *item = NULL;

    if (
        arguments_json != NULL
        && type != NULL
        && call_id != NULL
        && name != NULL
        && arguments != NULL
    ) {
        item = telos_value_new_object(keys, values, 4);
    }
    telos_value_release(arguments);
    telos_value_release(name);
    telos_value_release(call_id);
    telos_value_release(type);
    free(arguments_json);
    return item;
}

static struct telos_value *function_output_item(
    const struct pending_call *call
)
{
    char *output_json = value_json(call->result);
    struct telos_value *type =
        telos_value_new_string("function_call_output");
    struct telos_value *call_id = telos_value_new_string(call->call_id);
    struct telos_value *output = telos_value_new_string(output_json);
    const char *keys[] = {"type", "call_id", "output"};
    const struct telos_value *values[] = {type, call_id, output};
    struct telos_value *item = NULL;

    if (
        output_json != NULL
        && type != NULL
        && call_id != NULL
        && output != NULL
    ) {
        item = telos_value_new_object(keys, values, 3);
    }
    telos_value_release(output);
    telos_value_release(call_id);
    telos_value_release(type);
    free(output_json);
    return item;
}

static struct telos_value *continuation_items(
    const struct telos_value *prior_items,
    enum telos_provider_state_mode state_mode,
    const struct response_collector *collector
)
{
    struct telos_value **items;
    struct telos_value *result = NULL;
    size_t prior_count = state_mode == TELOS_PROVIDER_STATE_LOCAL
        ? telos_value_count(prior_items)
        : 0;
    size_t call_item_count = state_mode == TELOS_PROVIDER_STATE_LOCAL
        ? collector->call_count
        : 0;
    size_t item_count;

    if (
        prior_count > SIZE_MAX - call_item_count
        || prior_count + call_item_count
            > SIZE_MAX - collector->call_count
    ) {
        return NULL;
    }
    item_count = prior_count + call_item_count + collector->call_count;
    if (item_count > SIZE_MAX / sizeof(*items)) {
        return NULL;
    }
    items = calloc(item_count, sizeof(*items));
    if (items == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < prior_count; ++index) {
        items[index] = telos_value_retain(
            telos_value_at(prior_items, index)
        );
    }
    for (size_t index = 0; index < collector->call_count; ++index) {
        if (state_mode == TELOS_PROVIDER_STATE_LOCAL) {
            items[prior_count + index] = function_call_item(
                &collector->calls[index]
            );
        }
        items[prior_count + call_item_count + index] =
            function_output_item(&collector->calls[index]);
        if (
            (
                state_mode == TELOS_PROVIDER_STATE_LOCAL
                && items[prior_count + index] == NULL
            )
            || items[prior_count + call_item_count + index] == NULL
        ) {
            goto cleanup;
        }
    }
    result = telos_value_new_array(
        (const struct telos_value *const *)items,
        item_count
    );

cleanup:
    for (size_t index = 0; index < item_count; ++index) {
        telos_value_release(items[index]);
    }
    free(items);
    return result;
}

bool telos_agent_run(
    const struct telos_agent_options *options,
    const struct telos_provider_request *request,
    const struct telos_cancel *cancel,
    struct telos_agent_result *result,
    struct telos_error **error
)
{
    struct telos_provider_request current;
    struct telos_value *owned_items = NULL;
    char *owned_previous_response_id = NULL;
    struct response_collector collector = {0};
    size_t maximum_rounds;

    if (error != NULL) {
        *error = NULL;
    }
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (
        options == NULL
        || request == NULL
        || result == NULL
        || options->registry_generation == NULL
        || options->capability_broker == NULL
        || options->dispatch == NULL
        || request->items == NULL
        || telos_value_type(request->items) != TELOS_VALUE_ARRAY
        || request->tools == NULL
        || telos_value_type(request->tools) != TELOS_VALUE_ARRAY
        || request->options == NULL
        || telos_value_type(request->options) != TELOS_VALUE_OBJECT
        || (
            request->state_mode != TELOS_PROVIDER_STATE_LOCAL
            && request->state_mode != TELOS_PROVIDER_STATE_REMOTE
        )
    ) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Agent loop arguments are invalid"
        );
        return false;
    }
    maximum_rounds = options->maximum_provider_rounds == 0
        ? 8
        : options->maximum_provider_rounds;
    current = *request;

    for (size_t round = 0; round < maximum_rounds; ++round) {
        if (telos_cancel_requested(cancel)) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_CANCELLED,
                ECANCELED,
                "Agent loop was cancelled"
            );
            goto failure;
        }
        collector.completed = false;
        if (
            !options->dispatch(
                &current,
                collect_event,
                &collector,
                options->provider_context,
                error
            )
            || !collector.completed
        ) {
            if (error == NULL || *error == NULL) {
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_PROTOCOL,
                    EPROTO,
                    "Provider response did not complete"
                );
            }
            goto failure;
        }
        result->provider_rounds += 1;
        if (collector.call_count == 0) {
            result->text = collector.text == NULL
                ? copy_text("")
                : collector.text;
            collector.text = NULL;
            if (result->text == NULL) {
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Agent result allocation failed"
                );
                goto failure;
            }
            collector_clear(&collector);
            telos_value_release(owned_items);
            free(owned_previous_response_id);
            return true;
        }

        if (!run_tools(options, cancel, &collector, error)) {
            goto failure;
        }
        if (
            current.state_mode == TELOS_PROVIDER_STATE_REMOTE
            && collector.response_id == NULL
        ) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_PROTOCOL,
                EPROTO,
                "Remote Provider continuation requires a response identifier"
            );
            goto failure;
        }
        result->tool_calls += collector.call_count;
        {
            struct telos_value *next_items = continuation_items(
                current.items,
                current.state_mode,
                &collector
            );

            telos_value_release(owned_items);
            owned_items = next_items;
        }
        if (owned_items == NULL) {
            set_error(
                error,
                TELOS_ERROR_DOMAIN_MEMORY,
                ENOMEM,
                "Tool output Items could not be constructed"
            );
            goto failure;
        }
        current.items = owned_items;
        if (current.state_mode == TELOS_PROVIDER_STATE_REMOTE) {
            free(owned_previous_response_id);
            owned_previous_response_id = copy_text(collector.response_id);
            if (owned_previous_response_id == NULL) {
                set_error(
                    error,
                    TELOS_ERROR_DOMAIN_MEMORY,
                    ENOMEM,
                    "Remote Provider response identifier allocation failed"
                );
                goto failure;
            }
            current.previous_response_id = owned_previous_response_id;
        }
        collector_clear(&collector);
    }

    set_error(
        error,
        TELOS_ERROR_DOMAIN_STATE,
        ELOOP,
        "Agent loop exceeded the Provider round limit"
    );

failure:
    collector_clear(&collector);
    telos_value_release(owned_items);
    free(owned_previous_response_id);
    telos_agent_result_clear(result);
    return false;
}

void telos_agent_result_clear(struct telos_agent_result *result)
{
    if (result == NULL) {
        return;
    }
    free(result->text);
    memset(result, 0, sizeof(*result));
}
