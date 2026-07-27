#ifndef TELOS_PLUGIN_H
#define TELOS_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/clock.h>
#include <telos/event.h>
#include <telos/registry.h>
#include <telos/value.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELOS_PLUGIN_ABI_VERSION 1U

enum telos_plugin_state {
    TELOS_PLUGIN_DISCOVERED = 1,
    TELOS_PLUGIN_VERIFIED,
    TELOS_PLUGIN_LOADED,
    TELOS_PLUGIN_INITIALIZING,
    TELOS_PLUGIN_ACTIVE,
    TELOS_PLUGIN_QUIESCING,
    TELOS_PLUGIN_STOPPED,
    TELOS_PLUGIN_UNLOADED,
};

struct telos_allocator_api_v1 {
    uint32_t struct_size;
    void *(*allocate)(size_t size);
    void *(*reallocate)(void *allocation, size_t size);
    void (*deallocate)(void *allocation);
};

struct telos_value_api_v1 {
    uint32_t struct_size;
    struct telos_value *(*retain)(const struct telos_value *value);
    void (*release)(const struct telos_value *value);
    struct telos_value *(*parse_json)(
        const char *json,
        size_t size,
        struct telos_error **error
    );
    bool (*write_json)(
        const struct telos_value *value,
        char *buffer,
        size_t buffer_size,
        size_t *written,
        struct telos_error **error
    );
};

struct telos_event_api_v1 {
    uint32_t struct_size;
    struct telos_event *(*retain)(const struct telos_event *event);
    void (*release)(const struct telos_event *event);
};

struct telos_host_api_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    void (*log)(void *context, int level, const char *message);
    const struct telos_allocator_api_v1 *allocator;
    const struct telos_value_api_v1 *value;
    const struct telos_event_api_v1 *event;
    struct telos_clock clock;
};

bool telos_host_api_v1_initialize(
    struct telos_host_api_v1 *host,
    void *context,
    void (*log)(void *context, int level, const char *message),
    struct telos_error **error
);

struct telos_plugin_registrar_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    bool (*add)(
        void *context,
        const struct telos_extension_descriptor *descriptor,
        struct telos_error **error
    );
};

typedef int (*telos_plugin_init_v1_fn)(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
);

struct telos_plugin_instance;
struct telos_plugin_module;

struct telos_plugin_instance *telos_plugin_instance_create(
    const char *id,
    struct telos_error **error
);

struct telos_plugin_instance *telos_plugin_instance_retain(
    const struct telos_plugin_instance *instance
);

void telos_plugin_instance_release(
    const struct telos_plugin_instance *instance
);

enum telos_plugin_state telos_plugin_instance_state(
    const struct telos_plugin_instance *instance
);

bool telos_plugin_instance_set_healthy(
    struct telos_plugin_instance *instance,
    bool healthy,
    struct telos_error **error
);

bool telos_plugin_instance_transition(
    struct telos_plugin_instance *instance,
    enum telos_plugin_state next,
    struct telos_error **error
);

struct telos_plugin_module *telos_plugin_module_load_inprocess(
    const char *path,
    const char *plugin_id,
    const struct telos_host_api_v1 *host,
    struct telos_registry *registry,
    struct telos_error **error
);

void telos_plugin_module_destroy(struct telos_plugin_module *module);

const struct telos_plugin_instance *telos_plugin_module_instance(
    const struct telos_plugin_module *module
);

#ifdef __cplusplus
}
#endif

#endif
