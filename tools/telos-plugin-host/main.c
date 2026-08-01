#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <telos/plugin.h>
#include <telos/rpc.h>
#include <telos/tool.h>

struct plugin_runtime {
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_plugin_module *module;
};

static void log_message(void *context, int level, const char *message)
{
    (void)context;
    fprintf(stderr, "plugin[%d]: %s\n", level, message);
}

static bool plugin_runtime_load(struct plugin_runtime *runtime,
                                const char *path,
                                const char *plugin_id,
                                struct telos_error **error)
{
    struct telos_host_api_v1 host;

    runtime->registry = telos_registry_create(NULL, 0, error);
    if (runtime->registry == NULL ||
        !telos_host_api_v1_initialize(&host, NULL, log_message, error)) {
        return false;
    }
    runtime->module = telos_plugin_module_load_inprocess(
        path, plugin_id, &host, runtime->registry, error);
    if (runtime->module == NULL) {
        return false;
    }
    runtime->generation = telos_registry_acquire(runtime->registry);
    return runtime->generation != NULL;
}

static void plugin_runtime_clear(struct plugin_runtime *runtime)
{
    telos_registry_generation_release(runtime->generation);
    telos_plugin_module_destroy(runtime->module);
    telos_registry_destroy(runtime->registry);
    memset(runtime, 0, sizeof(*runtime));
}

static struct telos_value *response(const char *type,
                                    const struct telos_value *body)
{
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *type_value = telos_value_new_string(type);
    const char *keys[] = {"version", "type", "body"};
    const struct telos_value *values[] = {version, type_value, body};
    struct telos_value *message = telos_value_new_object(keys, values, 3);

    telos_value_release(type_value);
    telos_value_release(version);
    return message;
}

static struct telos_value *response_without_body(const char *type)
{
    struct telos_value *version = telos_value_new_integer(TELOS_RPC_VERSION);
    struct telos_value *type_value = telos_value_new_string(type);
    const char *keys[] = {"version", "type"};
    const struct telos_value *values[] = {version, type_value};
    struct telos_value *message = telos_value_new_object(keys, values, 2);

    telos_value_release(type_value);
    telos_value_release(version);
    return message;
}

static struct telos_value *execute_tool(const struct plugin_runtime *runtime,
                                        const struct telos_value *request_body,
                                        struct telos_error **error)
{
    const char *id = telos_value_string(telos_value_get(request_body, "id"));
    const struct telos_value *arguments =
        telos_value_get(request_body, "arguments");
    const struct telos_extension_descriptor *descriptor =
        telos_registry_generation_find(runtime->generation,
                                       TELOS_EXTENSION_TOOL, id);
    const struct telos_tool_definition *tool =
        descriptor == NULL ? NULL : descriptor->implementation;
    const struct telos_tool_context context = {0};
    struct telos_value *result = NULL;

    if (id == NULL || arguments == NULL || tool == NULL || tool->id == NULL ||
        strcmp(tool->id, id) != 0 || tool->execute == NULL) {
        *error = telos_error_create(TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                                    "Process Tool is unavailable", NULL);
        return NULL;
    }
    if (!tool->execute(&context, arguments, &result, error)) {
        if (*error == NULL) {
            *error = telos_error_create(TELOS_ERROR_DOMAIN_PLUGIN, EIO,
                                        "Process Tool failed without an error",
                                        NULL);
        }
        telos_value_release(result);
        return NULL;
    }
    if (result == NULL) {
        *error = telos_error_create(TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                                    "Process Tool returned no result", NULL);
    }
    return result;
}

int main(int argc, char **argv)
{
    struct plugin_runtime runtime = {0};
    bool plugin_loaded = false;

    if (argc != 1 && argc != 3 && argc != 4) {
        fputs("usage: telos-plugin-host [PLUGIN.so PLUGIN_ID]\n"
              "       telos-plugin-host --health-check PLUGIN.so PLUGIN_ID\n",
              stderr);
        return 2;
    }
    if (argc == 4) {
        struct telos_error *error = NULL;
        bool healthy = strcmp(argv[1], "--health-check") == 0 &&
                       plugin_runtime_load(&runtime, argv[2], argv[3], &error);

        if (!healthy) {
            fprintf(stderr, "telos-plugin-host: %s\n",
                    error == NULL ? "invalid health-check arguments"
                                  : telos_error_message(error));
        }
        telos_error_release(error);
        plugin_runtime_clear(&runtime);
        return healthy ? 0 : 1;
    }
    if (argc == 3) {
        struct telos_error *error = NULL;

        plugin_loaded = plugin_runtime_load(&runtime, argv[1], argv[2], &error);
        if (!plugin_loaded) {
            fprintf(stderr, "telos-plugin-host: %s\n",
                    telos_error_message(error));
            telos_error_release(error);
            plugin_runtime_clear(&runtime);
            return 1;
        }
    }

    for (;;) {
        struct telos_error *error = NULL;
        struct telos_value *request =
            telos_rpc_read_frame(0, TELOS_RPC_MAX_FRAME_SIZE, &error);
        const char *type;
        struct telos_value *body;
        struct telos_value *message;
        bool stopping = false;

        if (request == NULL) {
            fprintf(stderr, "telos-plugin-host: %s\n",
                    telos_error_message(error));
            telos_error_release(error);
            plugin_runtime_clear(&runtime);
            return 1;
        }
        type = telos_value_string(telos_value_get(request, "type"));
        if (strcmp(type, "health") == 0) {
            body = telos_value_new_string("healthy");
            message = response("health.result", body);
        } else if (strcmp(type, "echo") == 0) {
            const struct telos_value *request_body =
                telos_value_get(request, "body");

            body = request_body == NULL ? telos_value_new_null()
                                        : telos_value_retain(request_body);
            message = response("echo.result", body);
        } else if (strcmp(type, "tool.execute") == 0 && plugin_loaded) {
            struct telos_error *tool_error = NULL;

            body = execute_tool(&runtime, telos_value_get(request, "body"),
                                &tool_error);
            if (body == NULL) {
                body = telos_value_new_string(
                    tool_error == NULL ? "Process Tool failed"
                                       : telos_error_message(tool_error));
                message = response("error", body);
                telos_error_release(tool_error);
            } else {
                message = response("tool.execute.result", body);
            }
        } else if (strcmp(type, "shutdown") == 0) {
            body = telos_value_new_string("stopped");
            message = response("shutdown.result", body);
            stopping = true;
        } else if (strcmp(type, "crash") == 0) {
            telos_value_release(request);
            plugin_runtime_clear(&runtime);
            return 70;
        } else if (strcmp(type, "partial") == 0) {
            const unsigned char partial[] = {0, 0, 0, 100, '{'};

            telos_value_release(request);
            if (write(1, partial, sizeof(partial)) < 0) {
                plugin_runtime_clear(&runtime);
                return 1;
            }
            for (;;) {
                pause();
            }
        } else if (strcmp(type, "hang") == 0) {
            for (;;) {
                pause();
            }
        } else if (strcmp(type, "wrong-response") == 0) {
            body = telos_value_new_string("wrong");
            message = response("different.result", body);
        } else if (strcmp(type, "missing-body") == 0) {
            body = NULL;
            message = response_without_body("missing-body.result");
        } else if (strcmp(type, "error-nonstring") == 0) {
            body = telos_value_new_null();
            message = response("error", body);
        } else if (strcmp(type, "malformed") == 0) {
            const unsigned char malformed[] = {0, 0, 0, 4, 'n', 'o', 'p', 'e'};

            telos_value_release(request);
            if (write(1, malformed, sizeof(malformed)) < 0) {
                plugin_runtime_clear(&runtime);
                return 1;
            }
            plugin_runtime_clear(&runtime);
            return 0;
        } else if (strcmp(type, "oversized") == 0) {
            const unsigned char oversized[] = {0, 16, 0, 1};

            telos_value_release(request);
            if (write(1, oversized, sizeof(oversized)) < 0) {
                plugin_runtime_clear(&runtime);
                return 1;
            }
            plugin_runtime_clear(&runtime);
            return 0;
        } else {
            body = telos_value_new_string("unknown message type");
            message = response("error", body);
        }
        telos_value_release(body);
        telos_value_release(request);

        if (message == NULL || !telos_rpc_write_frame(1, message, &error)) {
            fprintf(stderr, "telos-plugin-host: %s\n",
                    telos_error_message(error));
            telos_error_release(error);
            telos_value_release(message);
            plugin_runtime_clear(&runtime);
            return 1;
        }
        telos_value_release(message);
        if (stopping) {
            plugin_runtime_clear(&runtime);
            return 0;
        }
    }
}
