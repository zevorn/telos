#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <telos/manifest.h>

static void write_file(const char *path, const char *content)
{
    FILE *stream = fopen(path, "wb");

    assert(stream != NULL);
    assert(fwrite(content, 1, strlen(content), stream) == strlen(content));
    assert(fclose(stream) == 0);
}

int main(int argc, char **argv)
{
    char directory[] = "/tmp/telos-manifest-XXXXXX";
    char lock_path[256];
    char copy_path[256];
    char dependency_path[256];
    char invalid_path[256];
    struct telos_error *error = NULL;
    struct telos_plugin_manifest *manifest;
    struct telos_plugin_lock *lock;
    struct telos_plugin_lock *copy;

    assert(argc == 2);
    manifest = telos_plugin_manifest_load(argv[1], &error);
    assert(manifest != NULL);
    assert(error == NULL);
    assert(strcmp(
        telos_plugin_manifest_id(manifest),
        "dev.example.echo-tool"
    ) == 0);
    assert(telos_plugin_manifest_abi(manifest) == 1);
    assert(
        telos_plugin_manifest_default_runtime(manifest)
        == TELOS_PLUGIN_RUNTIME_PROCESS
    );
    assert(
        telos_plugin_manifest_runtime_modes(manifest)
        & TELOS_PLUGIN_RUNTIME_STATIC
    );
    telos_plugin_manifest_destroy(manifest);

    assert(mkdtemp(directory) != NULL);
    snprintf(lock_path, sizeof(lock_path), "%s/telos.lock", directory);
    snprintf(copy_path, sizeof(copy_path), "%s/copy.lock", directory);
    snprintf(
        dependency_path,
        sizeof(dependency_path),
        "%s/dependency.txt",
        directory
    );
    snprintf(invalid_path, sizeof(invalid_path), "%s/invalid.toml", directory);
    write_file(dependency_path, "hello\n");
    write_file(
        lock_path,
        "format = 1\n"
        "source_hash = "
        "\"0000000000000000000000000000000000000000000000000000000000000000\"\n"
        "\n"
        "[[dependency]]\n"
        "name = \"fixture\"\n"
        "version = \"1.0.0\"\n"
        "source = \"dependency.txt\"\n"
        "sha256 = "
        "\"5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03\"\n"
    );
    lock = telos_plugin_lock_load(lock_path, &error);
    assert(lock != NULL);
    assert(error == NULL);
    assert(telos_plugin_lock_dependency_count(lock) == 1);
    assert(telos_plugin_lock_verify(lock, directory, &error));
    assert(error == NULL);
    assert(telos_plugin_lock_write(lock, copy_path, &error));
    copy = telos_plugin_lock_load(copy_path, &error);
    assert(copy != NULL);
    assert(telos_plugin_lock_verify(copy, directory, &error));
    telos_plugin_lock_destroy(copy);

    write_file(dependency_path, "tampered\n");
    assert(!telos_plugin_lock_verify(lock, directory, &error));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    telos_plugin_lock_destroy(lock);

    write_file(
        invalid_path,
        "[plugin]\n"
        "id = \"Invalid ID\"\n"
        "name = \"bad\"\n"
        "version = \"1\"\n"
        "abi = 9\n"
        "entry = \"wrong\"\n"
    );
    manifest = telos_plugin_manifest_load(invalid_path, &error);
    assert(manifest == NULL);
    assert(error != NULL);
    telos_error_release(error);

    unlink(invalid_path);
    unlink(dependency_path);
    unlink(copy_path);
    unlink(lock_path);
    rmdir(directory);
    return 0;
}
