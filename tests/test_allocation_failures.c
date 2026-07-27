#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/cancel.h>
#include <telos/config.h>
#include <telos/manifest.h>
#include <telos/plugin.h>
#include <telos/prompt.h>
#include <telos/resource.h>
#include <telos/secret.h>
#include <telos/store.h>
#include <telos/trace.h>

static size_t allocation_count;
static size_t allocation_to_fail;
static bool allocation_failure_enabled;
static bool allocation_failed;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *allocation, size_t size);

static bool should_fail(void)
{
    if (!allocation_failure_enabled) {
        return false;
    }
    allocation_count += 1;
    if (allocation_count == allocation_to_fail) {
        allocation_failed = true;
        return true;
    }
    return false;
}

void *__wrap_malloc(size_t size)
{
    return should_fail() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    return should_fail() ? NULL : __real_calloc(count, size);
}

void *__wrap_realloc(void *allocation, size_t size)
{
    return should_fail() ? NULL : __real_realloc(allocation, size);
}

typedef bool (*operation_fn)(void *context);

static void exercise_allocation_failures(
    operation_fn operation,
    void *context
)
{
    for (size_t failure = 1; failure < 1024; ++failure) {
        bool result;

        allocation_count = 0;
        allocation_to_fail = failure;
        allocation_failed = false;
        allocation_failure_enabled = true;
        result = operation(context);
        allocation_failure_enabled = false;
        if (!allocation_failed) {
            assert(result);
            return;
        }
    }
    assert(!"allocation failure sweep did not converge");
}

static bool json_operation(void *context)
{
    static const char json[] =
        "{\"long\":\"abcdefghijklmnopqrstuvwxyz0123456789\","
        "\"array\":[0,1,2,3,4],"
        "\"object\":{\"a\":0,\"b\":1,\"c\":2,\"d\":3,\"e\":4},"
        "\"unicode\":\"\\u00e9\\u20ac\\ud83d\\ude00\"}";
    struct telos_error *error = NULL;
    struct telos_value *value;
    char output[1024];
    bool result;

    (void)context;
    value = telos_value_parse_json(json, sizeof(json) - 1, &error);
    result = value != NULL
        && telos_value_write_json(
            value,
            output,
            sizeof(output),
            NULL,
            &error
        );
    telos_value_release(value);
    telos_error_release(error);
    return result;
}

static bool cancel_operation(void *context)
{
    struct telos_cancel *cancel = telos_cancel_create();
    bool result = cancel != NULL;

    (void)context;
    telos_cancel_release(cancel);
    return result;
}

static bool error_operation(void *context)
{
    struct telos_error *cause = telos_error_create(
        TELOS_ERROR_DOMAIN_IO,
        1,
        "cause",
        NULL
    );
    struct telos_error *outer = cause == NULL
        ? NULL
        : telos_error_create(
            TELOS_ERROR_DOMAIN_STATE,
            2,
            context,
            cause
        );
    bool result = outer != NULL;

    telos_error_release(outer);
    telos_error_release(cause);
    return result;
}

static bool prompt_operation(void *context)
{
    const char *content = context;
    const struct telos_prompt_fragment fragments[] = {
        {
            .slot = TELOS_PROMPT_POLICY,
            .trust = TELOS_PROMPT_TRUST_POLICY,
            .priority = 1,
            .byte_budget = 2048,
            .source = "policy",
            .content = content,
        },
        {
            .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .priority = 2,
            .byte_budget = 2048,
            .source = "project",
            .content = content,
        },
    };
    struct telos_error *error = NULL;
    struct telos_prompt_snapshot *snapshot = telos_prompt_snapshot_create(
        fragments,
        2,
        &error
    );
    bool result = snapshot != NULL;

    telos_prompt_snapshot_release(snapshot);
    telos_error_release(error);
    return result;
}

static bool resource_operation(void *context)
{
    const char *roots[] = {context};
    struct telos_error *error = NULL;
    struct telos_resource_manager *manager = telos_resource_manager_create(
        roots,
        1,
        &error
    );
    bool result = manager != NULL;

    telos_resource_manager_destroy(manager);
    telos_error_release(error);
    return result;
}

static char *resolve_secret(
    const char *reference,
    const char *target,
    void *context,
    struct telos_error **error
)
{
    const char *value = context;
    char *copy = malloc(strlen(value) + 1);

    (void)reference;
    (void)target;
    (void)error;
    if (copy != NULL) {
        strcpy(copy, value);
    }
    return copy;
}

static bool secret_operation(void *context)
{
    const char *capabilities[] = {"secret.use:provider.openai"};
    struct telos_error *error = NULL;
    struct telos_secret_reference *reference =
        telos_secret_reference_create("secret:provider.openai", &error);
    struct telos_secret_broker *broker = reference == NULL
        ? NULL
        : telos_secret_broker_create(
            resolve_secret,
            context,
            &error
        );
    struct telos_secret_material *material = broker == NULL
        ? NULL
        : telos_secret_broker_resolve(
            broker,
            reference,
            "provider.openai",
            capabilities,
            1,
            true,
            &error
        );
    bool result = material != NULL;

    telos_secret_material_destroy(material);
    telos_secret_broker_destroy(broker);
    telos_secret_reference_destroy(reference);
    telos_error_release(error);
    return result;
}

static bool registry_operation(void *context)
{
    const char *capabilities[] = {"process.spawn", "filesystem.read"};
    const char *required[] = {"process.spawn"};
    static int implementation;
    struct telos_error *error = NULL;
    struct telos_registry *registry = telos_registry_create(
        capabilities,
        2,
        &error
    );
    struct telos_registry_transaction *transaction = registry == NULL
        ? NULL
        : telos_registry_transaction_begin(
            registry,
            "fixture.plugin",
            &error
        );
    const struct telos_extension_descriptor descriptor = {
        .id = "fixture.tool",
        .plugin_id = "fixture.plugin",
        .kind = TELOS_EXTENSION_TOOL,
        .required_capabilities = required,
        .required_capability_count = 1,
        .implementation = &implementation,
    };
    bool result = transaction != NULL
        && telos_registry_transaction_add(
            transaction,
            &descriptor,
            &error
        )
        && telos_registry_transaction_commit(transaction, &error);

    (void)context;
    if (!result) {
        telos_registry_transaction_abort(transaction);
    }
    telos_registry_destroy(registry);
    telos_error_release(error);
    return result;
}

static bool plugin_operation(void *context)
{
    struct telos_error *error = NULL;
    struct telos_plugin_instance *instance = telos_plugin_instance_create(
        context,
        &error
    );
    bool result = instance != NULL;

    telos_plugin_instance_release(instance);
    telos_error_release(error);
    return result;
}

static bool store_operation(void *context)
{
    struct telos_error *error = NULL;
    struct telos_event_store *store = telos_memory_store_create(&error);
    bool result = store != NULL;

    (void)context;
    for (uint64_t sequence = 1; result && sequence <= 12; ++sequence) {
        struct telos_value *payload = telos_value_new_string("payload");
        const struct telos_event_spec spec = {
            .sequence = sequence,
            .event_id = telos_id_generate(),
            .session_id = telos_id_generate(),
            .correlation_id = telos_id_generate(),
            .causation_id = telos_id_generate(),
            .type = "fixture.event",
            .source = "allocation-test",
            .timestamp_milliseconds = (int64_t)sequence,
            .payload = payload,
        };
        struct telos_event *event = payload == NULL
            ? NULL
            : telos_event_create(&spec, &error);

        result = event != NULL
            && telos_event_store_append(store, event, &error);
        telos_event_release(event);
        telos_value_release(payload);
    }
    telos_event_store_destroy(store);
    telos_error_release(error);
    return result;
}

static bool ring_store_operation(void *context)
{
    struct telos_error *error = NULL;
    struct telos_event_store *store = telos_ring_store_create(4, &error);
    bool result = store != NULL;

    (void)context;
    telos_event_store_destroy(store);
    telos_error_release(error);
    return result;
}

static bool markdown_store_operation(void *context)
{
    const char *path = context;
    struct telos_error *error = NULL;
    struct telos_event_store *store;
    struct telos_value *payload;
    struct telos_event *event;
    bool result;

    unlink(path);
    store = telos_markdown_store_create(path, &error);
    payload = store == NULL ? NULL : telos_value_new_string("payload");
    if (payload != NULL) {
        const struct telos_event_spec spec = {
            .sequence = 1,
            .event_id = telos_id_generate(),
            .session_id = telos_id_generate(),
            .correlation_id = telos_id_generate(),
            .causation_id = telos_id_generate(),
            .type = "fixture.event",
            .source = "allocation-test",
            .timestamp_milliseconds = 1,
            .payload = payload,
        };

        event = telos_event_create(&spec, &error);
    } else {
        event = NULL;
    }
    result = event != NULL
        && telos_event_store_append(store, event, &error);
    telos_event_release(event);
    telos_value_release(payload);
    telos_event_store_destroy(store);
    telos_error_release(error);
    return result;
}

static bool trace_operation(void *context)
{
    struct telos_error *error = NULL;
    struct telos_value *payload = telos_value_new_string("payload");
    struct telos_event *event;
    char output[1024];
    size_t size;
    bool result;

    (void)context;
    if (payload != NULL) {
        const struct telos_event_spec spec = {
            .sequence = 1,
            .event_id = telos_id_generate(),
            .session_id = telos_id_generate(),
            .correlation_id = telos_id_generate(),
            .causation_id = telos_id_generate(),
            .type = "fixture.event",
            .source = "allocation-test",
            .timestamp_milliseconds = 1,
            .payload = payload,
        };

        event = telos_event_create(&spec, &error);
    } else {
        event = NULL;
    }
    size = telos_event_trace_json_size(event);
    result = size > 0
        && size <= sizeof(output)
        && telos_event_write_trace_json(
            event,
            output,
            sizeof(output),
            NULL,
            &error
        );
    telos_event_release(event);
    telos_value_release(payload);
    telos_error_release(error);
    return result;
}

struct manifest_paths {
    const char *manifest;
    const char *lock;
};

static bool manifest_operation(void *context)
{
    const struct manifest_paths *paths = context;
    struct telos_error *error = NULL;
    struct telos_plugin_manifest *manifest = telos_plugin_manifest_load(
        paths->manifest,
        &error
    );
    struct telos_plugin_lock *lock = manifest == NULL
        ? NULL
        : telos_plugin_lock_load(paths->lock, &error);
    bool result = manifest != NULL && lock != NULL;

    telos_plugin_lock_destroy(lock);
    telos_plugin_manifest_destroy(manifest);
    telos_error_release(error);
    return result;
}

struct config_paths {
    const char *home;
    const char *project;
};

static bool config_operation(void *context)
{
    const struct config_paths *paths = context;
    struct telos_error *error = NULL;
    struct telos_config *config = telos_config_load(
        paths->home,
        paths->project,
        &error
    );
    bool result = config != NULL;

    telos_config_destroy(config);
    telos_error_release(error);
    return result;
}

static void write_text(const char *path, const char *content)
{
    FILE *stream = fopen(path, "wb");

    assert(stream != NULL);
    assert(fwrite(content, 1, strlen(content), stream) == strlen(content));
    assert(fclose(stream) == 0);
}

int main(int argc, char **argv)
{
    char root[] = "/tmp/telos-allocation-XXXXXX";
    char skill_directory[512];
    char skill_path[512];
    char home[512];
    char telos_home[512];
    char user_config[512];
    char project[512];
    char project_config[512];
    char markdown_path[512];
    char prompt_content[1025];
    struct manifest_paths manifests;
    struct config_paths configs;

    assert(argc == 3);
    assert(mkdtemp(root) != NULL);
    assert(snprintf(
        skill_directory,
        sizeof(skill_directory),
        "%s/skill",
        root
    ) < (int)sizeof(skill_directory));
    assert(snprintf(
        skill_path,
        sizeof(skill_path),
        "%s/SKILL.md",
        skill_directory
    ) < (int)sizeof(skill_path));
    assert(snprintf(home, sizeof(home), "%s/home", root) < (int)sizeof(home));
    assert(snprintf(
        telos_home,
        sizeof(telos_home),
        "%s/.telos",
        home
    ) < (int)sizeof(telos_home));
    assert(snprintf(
        user_config,
        sizeof(user_config),
        "%s/config.toml",
        telos_home
    ) < (int)sizeof(user_config));
    assert(snprintf(
        project,
        sizeof(project),
        "%s/project",
        root
    ) < (int)sizeof(project));
    assert(snprintf(
        project_config,
        sizeof(project_config),
        "%s/telos.toml",
        project
    ) < (int)sizeof(project_config));
    assert(snprintf(
        markdown_path,
        sizeof(markdown_path),
        "%s/events.md",
        root
    ) < (int)sizeof(markdown_path));
    assert(mkdir(skill_directory, 0700) == 0);
    assert(mkdir(home, 0700) == 0);
    assert(mkdir(telos_home, 0700) == 0);
    assert(mkdir(project, 0700) == 0);
    write_text(
        skill_path,
        "---\n"
        "name: allocation-skill\n"
        "description: Exercise every Skill allocation.\n"
        "---\n"
        "Use the allocation fixture.\n"
    );
    write_text(
        user_config,
        "[agent]\nprovider = \"user-provider\"\nmodel = \"user-model\"\n"
    );
    write_text(
        project_config,
        "[builder]\nbackend = \"native\"\n"
        "[providers.openai]\nstate_mode = \"remote\"\n"
    );
    memset(prompt_content, 'x', sizeof(prompt_content) - 1);
    prompt_content[sizeof(prompt_content) - 1] = '\0';

    manifests = (struct manifest_paths) {
        .manifest = argv[1],
        .lock = argv[2],
    };
    configs = (struct config_paths) {
        .home = home,
        .project = project,
    };
    exercise_allocation_failures(cancel_operation, NULL);
    exercise_allocation_failures(error_operation, "outer");
    exercise_allocation_failures(json_operation, NULL);
    exercise_allocation_failures(prompt_operation, prompt_content);
    exercise_allocation_failures(resource_operation, root);
    exercise_allocation_failures(secret_operation, "secret-material");
    exercise_allocation_failures(registry_operation, NULL);
    exercise_allocation_failures(plugin_operation, "fixture.plugin");
    exercise_allocation_failures(store_operation, NULL);
    exercise_allocation_failures(ring_store_operation, NULL);
    exercise_allocation_failures(markdown_store_operation, markdown_path);
    exercise_allocation_failures(trace_operation, NULL);
    exercise_allocation_failures(manifest_operation, &manifests);
    exercise_allocation_failures(config_operation, &configs);

    unlink(project_config);
    unlink(user_config);
    unlink(skill_path);
    unlink(markdown_path);
    rmdir(project);
    rmdir(telos_home);
    rmdir(home);
    rmdir(skill_directory);
    rmdir(root);
    return 0;
}
