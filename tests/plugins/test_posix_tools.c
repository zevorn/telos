#define _XOPEN_SOURCE 700

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/plugins/posix_tools.h>
#include <telos/tool.h>

static enum telos_policy_decision
allow_tool(const struct telos_policy_request *request, void *context)
{
    (void)request;
    (void)context;
    return TELOS_POLICY_ALLOW;
}

static struct telos_value *json(const char *text)
{
    return telos_value_parse_json(text, strlen(text), NULL);
}

static bool execute(struct telos_registry_generation *generation,
                    struct telos_capability_broker *broker,
                    const char *name,
                    const char *arguments,
                    struct telos_value **result)
{
    struct telos_value *value = json(arguments);
    struct telos_error *error = NULL;
    bool passed = value != NULL && telos_tool_execute(
                                      generation, broker,
                                      TELOS_EXECUTION_CORE, name, value, NULL,
                                      result, &error);

    telos_error_release(error);
    telos_value_release(value);
    return passed;
}

int main(void)
{
    const char *capabilities[] = {
        "filesystem.read",
        "filesystem.write",
        "process.spawn",
    };
    char directory[] = "/tmp/telos-posix-tools-XXXXXX";
    struct telos_posix_tools_config config;
    struct telos_posix_tools *tools;
    struct telos_registry *registry;
    struct telos_registry_generation *generation;
    struct telos_capability_broker *broker;
    struct telos_value *result = NULL;
    struct telos_value *descriptions;
    char path[256];
    bool passed = false;

    assert(mkdtemp(directory) != NULL);
    config.working_directory = directory;
    config.shell = "/bin/sh";
    tools = telos_posix_tools_create(&config, NULL);
    registry = telos_registry_create(capabilities, 3, NULL);
    assert(tools != NULL && registry != NULL);
    assert(telos_posix_tools_register(tools, registry, NULL));
    generation = telos_registry_acquire(registry);
    broker = telos_capability_broker_create(capabilities, 3, allow_tool, NULL,
                                            NULL);
    descriptions = telos_posix_tools_describe(NULL);
    assert(generation != NULL && broker != NULL && descriptions != NULL);
    assert(telos_value_count(descriptions) == 4);
    assert(strcmp(telos_value_string(telos_value_get(
                       telos_value_at(descriptions, 0), "name")),
                   "read") == 0);

    assert(snprintf(path, sizeof(path), "%s/file.txt", directory) <
           (int)sizeof(path));
    {
        char arguments[512];

        assert(snprintf(arguments, sizeof(arguments),
                        "{\"path\":\"%s\",\"content\":\"one\\ntwo\\n\"}",
                        path) < (int)sizeof(arguments));
        assert(execute(generation, broker, "write", arguments, &result));
        telos_value_release(result);
        result = NULL;
    }
    {
        char arguments[256];

        assert(snprintf(arguments, sizeof(arguments), "{\"path\":\"%s\"}",
                        path) < (int)sizeof(arguments));
        assert(execute(generation, broker, "read", arguments, &result));
        assert(strcmp(telos_value_string(telos_value_get(result, "content")),
                      "one\ntwo\n") == 0);
        telos_value_release(result);
        result = NULL;
    }
    {
        char arguments[512];

        assert(snprintf(arguments, sizeof(arguments),
                        "{\"path\":\"%s\",\"old_text\":\"one\","
                        "\"new_text\":\"three\"}",
                        path) < (int)sizeof(arguments));
        assert(execute(generation, broker, "edit", arguments, &result));
        telos_value_release(result);
        result = NULL;
    }
    assert(execute(generation, broker, "bash",
                   "{\"command\":\"printf shell-output\"}", &result));
    assert(telos_value_integer(telos_value_get(result, "exit_code"),
                               &(int64_t){0}));
    assert(strcmp(telos_value_string(telos_value_get(result, "output")),
                  "shell-output") == 0);
    telos_value_release(result);
    result = NULL;
    assert(execute(generation, broker, "bash",
                   "{\"command\":\"yes x | head -c 300000\"}",
                   &result));
    {
        const char *output =
            telos_value_string(telos_value_get(result, "output"));

        assert(output != NULL);
        assert(strlen(output) < 256U * 1024U);
        assert(strstr(output, "[shell output truncated]") != NULL);
    }
    telos_value_release(result);
    result = NULL;
    assert(!execute(generation, broker, "read",
                    "{\"path\":\"../outside\"}", &result));

    passed = true;
    unlink(path);
    telos_value_release(result);
    telos_value_release(descriptions);
    telos_capability_broker_destroy(broker);
    telos_registry_generation_release(generation);
    telos_registry_destroy(registry);
    telos_posix_tools_destroy(tools);
    rmdir(directory);
    return passed ? 0 : 1;
}
