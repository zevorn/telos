#ifndef TELOS_PLUGIN_H
#define TELOS_PLUGIN_H

#include <telos/types.h>

#include <telos/clock.h>
#include <telos/event.h>
#include <telos/registry.h>
#include <telos/value.h>

#define TELOS_PLUGIN_ABI_VERSION 1U

typedef struct telos_host_api_v1 telos_host_api_v1;
typedef struct telos_plugin_instance telos_plugin_instance;
typedef struct telos_plugin_module telos_plugin_module;
typedef struct telos_plugin_registrar_v1 telos_plugin_registrar_v1;

typedef void (*telos_log_fn)(void *context, int level, const char *message);

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
    struct telos_value *(*parse_json)(const char *json,
                                      size_t size,
                                      struct telos_error **error);
    bool (*write_json)(const struct telos_value *value,
                       char *buffer,
                       size_t buffer_size,
                       size_t *written,
                       struct telos_error **error);
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
    telos_log_fn log;
    const struct telos_allocator_api_v1 *allocator;
    const struct telos_value_api_v1 *value;
    const struct telos_event_api_v1 *event;
    struct telos_clock clock;
};

bool telos_host_api_v1_initialize(telos_host_api_v1 *host, void *context,
                                  telos_log_fn log,
                                  struct telos_error **error);

struct telos_plugin_registrar_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void *context;
    bool (*add)(void *context,
                const struct telos_extension_descriptor *descriptor,
                struct telos_error **error);
};

typedef int (*telos_plugin_init_v1_fn)(const telos_host_api_v1 *host,
                                       telos_plugin_registrar_v1 *registrar);

telos_plugin_instance *
telos_plugin_instance_create(const char *id, struct telos_error **error);

telos_plugin_instance *
telos_plugin_instance_retain(const telos_plugin_instance *instance);

void telos_plugin_instance_release(const telos_plugin_instance *instance);

enum telos_plugin_state
telos_plugin_instance_state(const telos_plugin_instance *instance);

bool telos_plugin_instance_set_healthy(telos_plugin_instance *instance,
                                       bool healthy,
                                       struct telos_error **error);

bool telos_plugin_instance_transition(telos_plugin_instance *instance,
                                      enum telos_plugin_state next,
                                      struct telos_error **error);

telos_plugin_module *
telos_plugin_module_load_inprocess(const char *path,
                                   const char *plugin_id,
                                   const telos_host_api_v1 *host,
                                   struct telos_registry *registry,
                                   struct telos_error **error);

void telos_plugin_module_destroy(telos_plugin_module *module);

const telos_plugin_instance *
telos_plugin_module_instance(const telos_plugin_module *module);

#endif
