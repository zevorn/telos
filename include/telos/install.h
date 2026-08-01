#ifndef TELOS_INSTALL_H
#define TELOS_INSTALL_H

#include <telos/types.h>

#include <telos/cancel.h>
#include <telos/error.h>

enum telos_install_state {
    TELOS_INSTALL_RESOLVE = 1,
    TELOS_INSTALL_FETCH_TO_QUARANTINE,
    TELOS_INSTALL_INSPECT,
    TELOS_INSTALL_PLAN,
    TELOS_INSTALL_AUTHORIZE,
    TELOS_INSTALL_VERIFY_DEPENDENCIES,
    TELOS_INSTALL_BUILD,
    TELOS_INSTALL_TEST,
    TELOS_INSTALL_ABI_CHECK,
    TELOS_INSTALL_STAGE,
    TELOS_INSTALL_HEALTH_CHECK,
    TELOS_INSTALL_ACTIVATE,
    TELOS_INSTALL_COMMIT,
    TELOS_INSTALL_ROLLBACK,
    TELOS_INSTALL_COMPLETED,
};

enum telos_builder_backend {
    TELOS_BUILDER_NATIVE = 1,
    TELOS_BUILDER_CONTAINER,
};

enum telos_install_goal {
    TELOS_INSTALL_GOAL_INSTALL = 0,
    TELOS_INSTALL_GOAL_BUILD,
    TELOS_INSTALL_GOAL_TEST,
};

struct telos_install_risk {
    bool git_source;
    bool native_build;
    bool network;
    bool filesystem_write;
    bool process_spawn;
    bool secret_use;
    bool unlocked_source;
    size_t permission_count;
    bool requires_approval;
};

typedef bool (*telos_install_approve_fn)(const struct telos_install_risk *risk,
                                         void *context);

typedef void (*telos_install_progress_fn)(enum telos_install_state state,
                                          void *context);

struct telos_install_options {
    const char *source;
    const char *state_directory;
    const char *sdk_pkgconfig_path;
    const char *sdk_sysroot;
    const char *abi_check_path;
    const char *plugin_host_path;
    const char *container_image;
    enum telos_builder_backend builder;
    enum telos_install_goal goal;
    unsigned int timeout_seconds;
    telos_install_approve_fn approve;
    void *approve_context;
    telos_install_progress_fn progress;
    void *progress_context;
};

struct telos_install_result {
    char *plugin_id;
    char *version;
    char *cache_key;
    char *artifact_path;
    bool cache_hit;
};

bool telos_plugin_install(const struct telos_install_options *options,
                          const struct telos_cancel *cancel,
                          struct telos_install_result *result,
                          struct telos_error **error);

bool telos_plugin_rollback(const char *state_directory,
                           const char *plugin_id,
                           struct telos_error **error);

bool telos_plugin_activate(const char *state_directory,
                           const char *plugin_id,
                           const char *cache_key,
                           struct telos_error **error);

bool telos_plugin_remove(const char *state_directory,
                         const char *plugin_id,
                         struct telos_error **error);

void telos_install_result_clear(struct telos_install_result *result);

#endif
