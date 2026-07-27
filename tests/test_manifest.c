#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    char multiline_path[256];
    char source_directory[256];
    char source_lock_path[512];
    char source_payload_path[512];
    char source_digest[65];
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
    snprintf(
        multiline_path,
        sizeof(multiline_path),
        "%s/multiline.toml",
        directory
    );
    snprintf(
        source_directory,
        sizeof(source_directory),
        "%s/source",
        directory
    );
    snprintf(
        source_lock_path,
        sizeof(source_lock_path),
        "%s/telos.lock",
        source_directory
    );
    snprintf(
        source_payload_path,
        sizeof(source_payload_path),
        "%s/payload.txt",
        source_directory
    );
    write_file(dependency_path, "hello\n");
    assert(mkdir(source_directory, 0700) == 0);
    write_file(source_payload_path, "source payload\n");
    assert(telos_plugin_source_digest(
        source_directory,
        source_digest,
        &error
    ));
    {
        char source_lock[256];

        assert(snprintf(
            source_lock,
            sizeof(source_lock),
            "format = 1\nsource_hash = \"%s\"\n\ndependencies = []\n",
            source_digest
        ) < (int)sizeof(source_lock));
        write_file(source_lock_path, source_lock);
    }
    lock = telos_plugin_lock_load(source_lock_path, &error);
    assert(lock != NULL);
    assert(telos_plugin_lock_verify_source(
        lock,
        source_directory,
        &error
    ));
    write_file(source_payload_path, "tampered source\n");
    assert(!telos_plugin_lock_verify_source(
        lock,
        source_directory,
        &error
    ));
    assert(error != NULL);
    telos_error_release(error);
    error = NULL;
    telos_plugin_lock_destroy(lock);

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
        multiline_path,
        "[plugin]\n"
        "id = \"dev.example.multiline\"\n"
        "name = \"Multiline\"\n"
        "version = \"1.0.0\"\n"
        "abi = 1\n"
        "entry = \"telos_plugin_init_v1\"\n"
        "\n"
        "[runtime]\n"
        "modes = [\n"
        "    \"process\",\n"
        "    \"static\",\n"
        "]\n"
        "default = \"process\"\n"
        "\n"
        "[platform]\n"
        "targets = [\n"
        "    \"linux-x86_64\", # host fixture\n"
        "    \"zephyr-arm64\",\n"
        "]\n"
        "\n"
        "[build]\n"
        "system = \"meson\"\n"
        "\n"
        "[permissions]\n"
        "required = [\n"
        "    \"filesystem.read:project\",\n"
        "    \"network.http\",\n"
        "]\n"
    );
    manifest = telos_plugin_manifest_load(multiline_path, &error);
    assert(manifest != NULL);
    assert(error == NULL);
    assert(telos_plugin_manifest_permission_count(manifest) == 2);
    assert(strcmp(
        telos_plugin_manifest_permission_at(manifest, 1),
        "network.http"
    ) == 0);
    telos_plugin_manifest_destroy(manifest);

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
    unlink(multiline_path);
    unlink(source_payload_path);
    unlink(source_lock_path);
    rmdir(source_directory);
    unlink(dependency_path);
    unlink(copy_path);
    unlink(lock_path);
    rmdir(directory);
    return 0;
}
