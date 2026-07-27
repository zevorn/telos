#ifndef TELOS_MANIFEST_H
#define TELOS_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_plugin_runtime_mode {
    TELOS_PLUGIN_RUNTIME_BUILTIN = 1U << 0,
    TELOS_PLUGIN_RUNTIME_INPROCESS = 1U << 1,
    TELOS_PLUGIN_RUNTIME_PROCESS = 1U << 2,
    TELOS_PLUGIN_RUNTIME_STATIC = 1U << 3,
};

struct telos_plugin_manifest;
struct telos_plugin_lock;

struct telos_lock_dependency {
    const char *name;
    const char *version;
    const char *source;
    const char *sha256;
};

struct telos_plugin_manifest *telos_plugin_manifest_load(
    const char *path,
    struct telos_error **error
);

void telos_plugin_manifest_destroy(struct telos_plugin_manifest *manifest);

const char *telos_plugin_manifest_id(
    const struct telos_plugin_manifest *manifest
);

const char *telos_plugin_manifest_name(
    const struct telos_plugin_manifest *manifest
);

const char *telos_plugin_manifest_version(
    const struct telos_plugin_manifest *manifest
);

uint32_t telos_plugin_manifest_abi(
    const struct telos_plugin_manifest *manifest
);

unsigned int telos_plugin_manifest_runtime_modes(
    const struct telos_plugin_manifest *manifest
);

enum telos_plugin_runtime_mode telos_plugin_manifest_default_runtime(
    const struct telos_plugin_manifest *manifest
);

size_t telos_plugin_manifest_permission_count(
    const struct telos_plugin_manifest *manifest
);

const char *telos_plugin_manifest_permission_at(
    const struct telos_plugin_manifest *manifest,
    size_t index
);

struct telos_plugin_lock *telos_plugin_lock_load(
    const char *path,
    struct telos_error **error
);

void telos_plugin_lock_destroy(struct telos_plugin_lock *lock);

const char *telos_plugin_lock_source_hash(
    const struct telos_plugin_lock *lock
);

size_t telos_plugin_lock_dependency_count(
    const struct telos_plugin_lock *lock
);

const struct telos_lock_dependency *telos_plugin_lock_dependency_at(
    const struct telos_plugin_lock *lock,
    size_t index
);

bool telos_plugin_lock_verify(
    const struct telos_plugin_lock *lock,
    const char *base_directory,
    struct telos_error **error
);

bool telos_plugin_source_digest(
    const char *source_directory,
    char output[65],
    struct telos_error **error
);

bool telos_plugin_lock_verify_source(
    const struct telos_plugin_lock *lock,
    const char *source_directory,
    struct telos_error **error
);

bool telos_plugin_lock_write(
    const struct telos_plugin_lock *lock,
    const char *path,
    struct telos_error **error
);

#ifdef __cplusplus
}
#endif

#endif
