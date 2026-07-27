#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugin.h>

struct telos_plugin_instance {
    atomic_uint references;
    pthread_mutex_t mutex;
    char *id;
    enum telos_plugin_state state;
    bool healthy;
};

static const struct telos_allocator_api_v1 allocator_api = {
    .struct_size = sizeof(allocator_api),
    .allocate = malloc,
    .reallocate = realloc,
    .deallocate = free,
};

static const struct telos_value_api_v1 value_api = {
    .struct_size = sizeof(value_api),
    .retain = telos_value_retain,
    .release = telos_value_release,
    .parse_json = telos_value_parse_json,
    .write_json = telos_value_write_json,
};

static const struct telos_event_api_v1 event_api = {
    .struct_size = sizeof(event_api),
    .retain = telos_event_retain,
    .release = telos_event_release,
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

static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = malloc(size);

    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

bool telos_host_api_v1_initialize(
    struct telos_host_api_v1 *host,
    void *context,
    void (*log)(void *context, int level, const char *message),
    struct telos_error **error
)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (host == NULL || log == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin Host API and bootstrap logger are required"
        );
        return false;
    }
    *host = (struct telos_host_api_v1) {
        .abi_version = TELOS_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(*host),
        .context = context,
        .log = log,
        .allocator = &allocator_api,
        .value = &value_api,
        .event = &event_api,
        .clock = telos_system_clock(),
    };
    return true;
}

struct telos_plugin_instance *telos_plugin_instance_create(
    const char *id,
    struct telos_error **error
)
{
    struct telos_plugin_instance *instance;
    int result;

    if (error != NULL) {
        *error = NULL;
    }
    if (id == NULL || id[0] == '\0') {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin ID is required"
        );
        return NULL;
    }
    instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin instance allocation failed"
        );
        return NULL;
    }
    instance->id = copy_string(id);
    if (instance->id == NULL) {
        free(instance);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_MEMORY,
            ENOMEM,
            "Plugin ID allocation failed"
        );
        return NULL;
    }
    result = pthread_mutex_init(&instance->mutex, NULL);
    if (result != 0) {
        free(instance->id);
        free(instance);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            result,
            "Plugin mutex initialization failed"
        );
        return NULL;
    }
    atomic_init(&instance->references, 1);
    instance->state = TELOS_PLUGIN_DISCOVERED;
    return instance;
}

struct telos_plugin_instance *telos_plugin_instance_retain(
    const struct telos_plugin_instance *instance
)
{
    struct telos_plugin_instance *mutable_instance =
        (struct telos_plugin_instance *)instance;

    if (mutable_instance != NULL) {
        atomic_fetch_add_explicit(
            &mutable_instance->references,
            1,
            memory_order_relaxed
        );
    }
    return mutable_instance;
}

void telos_plugin_instance_release(
    const struct telos_plugin_instance *instance
)
{
    struct telos_plugin_instance *mutable_instance =
        (struct telos_plugin_instance *)instance;

    if (
        mutable_instance != NULL
        && atomic_fetch_sub_explicit(
            &mutable_instance->references,
            1,
            memory_order_acq_rel
        ) == 1
    ) {
        pthread_mutex_destroy(&mutable_instance->mutex);
        free(mutable_instance->id);
        free(mutable_instance);
    }
}

enum telos_plugin_state telos_plugin_instance_state(
    const struct telos_plugin_instance *instance
)
{
    struct telos_plugin_instance *mutable_instance =
        (struct telos_plugin_instance *)instance;
    enum telos_plugin_state state;

    if (mutable_instance == NULL) {
        return 0;
    }
    pthread_mutex_lock(&mutable_instance->mutex);
    state = mutable_instance->state;
    pthread_mutex_unlock(&mutable_instance->mutex);
    return state;
}

bool telos_plugin_instance_set_healthy(
    struct telos_plugin_instance *instance,
    bool healthy,
    struct telos_error **error
)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (instance == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin instance is required"
        );
        return false;
    }
    pthread_mutex_lock(&instance->mutex);
    if (instance->state != TELOS_PLUGIN_INITIALIZING) {
        pthread_mutex_unlock(&instance->mutex);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EINVAL,
            "Plugin health can only be set while initializing"
        );
        return false;
    }
    instance->healthy = healthy;
    pthread_mutex_unlock(&instance->mutex);
    return true;
}

static bool transition_allowed(
    enum telos_plugin_state current,
    enum telos_plugin_state next
)
{
    switch (current) {
    case TELOS_PLUGIN_DISCOVERED:
        return next == TELOS_PLUGIN_VERIFIED;
    case TELOS_PLUGIN_VERIFIED:
        return next == TELOS_PLUGIN_LOADED;
    case TELOS_PLUGIN_LOADED:
        return next == TELOS_PLUGIN_INITIALIZING;
    case TELOS_PLUGIN_INITIALIZING:
        return next == TELOS_PLUGIN_ACTIVE;
    case TELOS_PLUGIN_ACTIVE:
        return next == TELOS_PLUGIN_QUIESCING;
    case TELOS_PLUGIN_QUIESCING:
        return next == TELOS_PLUGIN_STOPPED;
    case TELOS_PLUGIN_STOPPED:
        return next == TELOS_PLUGIN_UNLOADED;
    default:
        return false;
    }
}

bool telos_plugin_instance_transition(
    struct telos_plugin_instance *instance,
    enum telos_plugin_state next,
    struct telos_error **error
)
{
    if (error != NULL) {
        *error = NULL;
    }
    if (instance == NULL) {
        set_error(
            error,
            TELOS_ERROR_DOMAIN_ARGUMENT,
            EINVAL,
            "Plugin instance is required"
        );
        return false;
    }

    pthread_mutex_lock(&instance->mutex);
    if (!transition_allowed(instance->state, next)) {
        pthread_mutex_unlock(&instance->mutex);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EINVAL,
            "Plugin lifecycle transition is invalid"
        );
        return false;
    }
    if (next == TELOS_PLUGIN_ACTIVE && !instance->healthy) {
        pthread_mutex_unlock(&instance->mutex);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_PLUGIN,
            EHOSTDOWN,
            "Plugin health check did not pass"
        );
        return false;
    }
    if (
        next == TELOS_PLUGIN_STOPPED
        && atomic_load_explicit(
            &instance->references,
            memory_order_acquire
        ) > 1
    ) {
        pthread_mutex_unlock(&instance->mutex);
        set_error(
            error,
            TELOS_ERROR_DOMAIN_STATE,
            EBUSY,
            "Plugin is still pinned by an active Session"
        );
        return false;
    }
    instance->state = next;
    pthread_mutex_unlock(&instance->mutex);
    return true;
}
