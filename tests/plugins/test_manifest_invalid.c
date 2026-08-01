#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/manifest.h>

#define ZERO_HASH                                                              \
    "0000000000000000000000000000000000000000000000000000000000000000"

struct manifest_fields {
    const char *id;
    const char *name;
    const char *version;
    const char *abi;
    const char *entry;
    const char *modes;
    const char *default_mode;
    const char *targets;
    const char *system;
    const char *permissions;
    const char *plugin_extra;
    const char *runtime_extra;
    const char *platform_extra;
    const char *build_extra;
    const char *permission_extra;
};

static const struct manifest_fields valid_fields = {
    .id = "\"dev.example.fixture\"",
    .name = "\"Fixture\"",
    .version = "\"1.0.0\"",
    .abi = "1",
    .entry = "\"telos_plugin_init_v1\"",
    .modes = "[\"builtin\", \"inprocess\", \"process\", \"static\"]",
    .default_mode = "\"process\"",
    .targets = "[\"linux-x86_64\", \"linux-aarch64\", "
               "\"linux-riscv64\", \"zephyr-arm64\", \"zephyr-native\"]",
    .system = "\"meson\"",
    .permissions = "[\"network.http\", \"secret.use:Provider_Key\"]",
    .plugin_extra = "",
    .runtime_extra = "",
    .platform_extra = "",
    .build_extra = "",
    .permission_extra = "",
};

static bool write_text(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(content);
    bool result = file != NULL && fwrite(content, 1, size, file) == size;

    if (file != NULL) {
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool write_manifest(const char *path,
                           const struct manifest_fields *fields)
{
    FILE *file = fopen(path, "wb");
    bool result;

    if (file == NULL) {
        return false;
    }
    result =
        fprintf(file,
                "[plugin]\n"
                "id = %s\n"
                "name = %s\n"
                "version = %s\n"
                "abi = %s\n"
                "entry = %s\n"
                "%s"
                "[runtime]\n"
                "modes = %s\n"
                "default = %s\n"
                "%s"
                "[platform]\n"
                "targets = %s\n"
                "%s"
                "[build]\n"
                "system = %s\n"
                "%s"
                "[permissions]\n"
                "required = %s\n"
                "%s",
                fields->id, fields->name, fields->version, fields->abi,
                fields->entry, fields->plugin_extra, fields->modes,
                fields->default_mode, fields->runtime_extra, fields->targets,
                fields->platform_extra, fields->system, fields->build_extra,
                fields->permissions, fields->permission_extra) > 0;
    return fclose(file) == 0 && result;
}

static bool manifest_rejected(const char *path, struct manifest_fields fields)
{
    struct telos_error *error = NULL;
    struct telos_plugin_manifest *manifest;
    bool rejected = write_manifest(path, &fields);

    manifest = telos_plugin_manifest_load(path, &error);
    rejected = rejected && manifest == NULL && error != NULL;
    telos_plugin_manifest_destroy(manifest);
    telos_error_release(error);
    return rejected;
}

static bool content_rejected(const char *path, const char *content)
{
    struct telos_error *error = NULL;
    struct telos_plugin_manifest *manifest;
    bool rejected = write_text(path, content);

    manifest = telos_plugin_manifest_load(path, &error);
    rejected = rejected && manifest == NULL && error != NULL;
    telos_plugin_manifest_destroy(manifest);
    telos_error_release(error);
    return rejected;
}

static bool lock_rejected(const char *path, const char *content)
{
    struct telos_error *error = NULL;
    struct telos_plugin_lock *lock;
    bool rejected = write_text(path, content);

    lock = telos_plugin_lock_load(path, &error);
    rejected = rejected && lock == NULL && error != NULL;
    telos_plugin_lock_destroy(lock);
    telos_error_release(error);
    return rejected;
}

static bool verify_path_rejected(const char *lock_path,
                                 const char *base_directory,
                                 const char *source)
{
    char content[1024];
    struct telos_plugin_lock *lock;
    struct telos_error *error = NULL;
    bool rejected;

    snprintf(content, sizeof(content),
             "format = 1\n"
             "source_hash = \"" ZERO_HASH "\"\n"
             "[[dependency]]\n"
             "name = \"dep\"\n"
             "version = \"1\"\n"
             "source = \"%s\"\n"
             "sha256 = \"" ZERO_HASH "\"\n",
             source);
    if (!write_text(lock_path, content)) {
        return false;
    }
    lock = telos_plugin_lock_load(lock_path, NULL);
    rejected = lock != NULL &&
               !telos_plugin_lock_verify(lock, base_directory, &error) &&
               error != NULL;
    telos_error_release(error);
    telos_plugin_lock_destroy(lock);
    return rejected;
}

static bool manifest_matrix(const char *path)
{
    static const char *invalid_ids[] = {
        "\"fixture\"",      "\".fixture\"",    "\"fixture.\"",
        "\"fixture..bad\"", "\"Fixture.bad\"", "\"fixture_bad.id\"",
    };
    static const char *invalid_modes[] = {
        "[]",          "[\"unknown\"]", "[\"process\", \"process\"]",
        "\"process\"", "[process]",     "[\"process\" trailing]",
    };
    static const char *invalid_targets[] = {
        "[]",
        "[\"unknown\"]",
        "\"linux-x86_64\"",
        "[1]",
    };
    static const char *invalid_permissions[] = {
        "[\"Network.http\"]",
        "[\"network:\"]",
        "[\"network:http:extra\"]",
        "[\"network_bad\"]",
        "[1]",
    };
    struct manifest_fields fields;
    struct telos_plugin_manifest *manifest;
    bool passed = true;

    for (size_t index = 0;
         passed && index < sizeof(invalid_ids) / sizeof(invalid_ids[0]);
         ++index) {
        fields = valid_fields;
        fields.id = invalid_ids[index];
        passed = manifest_rejected(path, fields);
    }
    fields = valid_fields;
    fields.name = "\"\"";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.version = "\"\"";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.abi = "0";
    passed = passed && manifest_rejected(path, fields);
    fields.abi = "2";
    passed = passed && manifest_rejected(path, fields);
    fields.abi = "not-number";
    passed = passed && manifest_rejected(path, fields);
    fields.abi = "4294967296";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.entry = "\"wrong\"";
    passed = passed && manifest_rejected(path, fields);

    for (size_t index = 0;
         passed && index < sizeof(invalid_modes) / sizeof(invalid_modes[0]);
         ++index) {
        fields = valid_fields;
        fields.modes = invalid_modes[index];
        passed = manifest_rejected(path, fields);
    }
    fields = valid_fields;
    fields.default_mode = "\"unknown\"";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.modes = "[\"static\"]";
    passed = passed && manifest_rejected(path, fields);

    for (size_t index = 0;
         passed && index < sizeof(invalid_targets) / sizeof(invalid_targets[0]);
         ++index) {
        fields = valid_fields;
        fields.targets = invalid_targets[index];
        passed = manifest_rejected(path, fields);
    }
    fields = valid_fields;
    fields.system = "\"cmake\"";
    passed = passed && manifest_rejected(path, fields);
    for (size_t index = 0; passed && index < sizeof(invalid_permissions) /
                                                 sizeof(invalid_permissions[0]);
         ++index) {
        fields = valid_fields;
        fields.permissions = invalid_permissions[index];
        passed = manifest_rejected(path, fields);
    }

    fields = valid_fields;
    fields.plugin_extra = "id = \"dev.example.duplicate\"\n";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.runtime_extra = "modes = [\"process\"]\n";
    passed = passed && manifest_rejected(path, fields);
    fields.runtime_extra = "default = \"process\"\n";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.platform_extra = "targets = [\"linux-x86_64\"]\n";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.build_extra = "system = \"meson\"\n";
    passed = passed && manifest_rejected(path, fields);
    fields = valid_fields;
    fields.permission_extra = "required = []\n";
    passed = passed && manifest_rejected(path, fields);

    passed = passed && content_rejected(path, "id = \"dev.example.bad\"\n") &&
             content_rejected(path, "[unknown]\nkey = \"value\"\n") &&
             content_rejected(path, "[plugin]\n[plugin]\n") &&
             content_rejected(path, "[plugin]\ninvalid\n") &&
             content_rejected(path, "[plugin]\n"
                                    "id = \"dev.example.bad\"\n"
                                    "[runtime]\n"
                                    "modes = [\n"
                                    "  \"process\"\n");

    fields = valid_fields;
    fields.permissions = "[]";
    passed = passed && write_manifest(path, &fields);
    manifest = telos_plugin_manifest_load(path, NULL);
    passed =
        passed && manifest != NULL &&
        strcmp(telos_plugin_manifest_name(manifest), "Fixture") == 0 &&
        strcmp(telos_plugin_manifest_version(manifest), "1.0.0") == 0 &&
        telos_plugin_manifest_runtime_modes(manifest) ==
            (TELOS_PLUGIN_RUNTIME_BUILTIN | TELOS_PLUGIN_RUNTIME_INPROCESS |
             TELOS_PLUGIN_RUNTIME_PROCESS | TELOS_PLUGIN_RUNTIME_STATIC) &&
        telos_plugin_manifest_permission_count(manifest) == 0 &&
        telos_plugin_manifest_permission_at(manifest, 0) == NULL;
    telos_plugin_manifest_destroy(manifest);
    return passed;
}

static bool lock_matrix(const char *lock_path,
                        const char *directory,
                        const char *escape_path)
{
    static const char *invalid_locks[] = {
        "",
        "format = 2\nsource_hash = \"" ZERO_HASH "\"\ndependencies = []\n",
        "format = 1\ndependencies = []\n",
        "format = 1\nsource_hash = \"short\"\ndependencies = []\n",
        "format = 1\nsource_hash = "
        "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"\n"
        "dependencies = []\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\ndependencies = bad\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\nunknown = \"x\"\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\ninvalid\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = \"dep\"\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = 1\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = \"a\"\nname = \"b\"\n"
        "version = \"1\"\nsource = \"dep\"\nsha256 = \"" ZERO_HASH "\"\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = \"a\"\nversion = \"1\"\n"
        "source = \"dep\"\nsha256 = \"short\"\n",
        "format = 1\nsource_hash = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = \"a\"\nversion = \"1\"\n"
        "source = \"dep\"\nsha256 = \"" ZERO_HASH "\"\n"
        "[[dependency]]\nname = \"incomplete\"\n",
    };
    struct telos_plugin_lock *lock;
    struct telos_error *error = NULL;
    char digest[65];
    bool passed = true;

    for (size_t index = 0;
         passed && index < sizeof(invalid_locks) / sizeof(invalid_locks[0]);
         ++index) {
        passed = lock_rejected(lock_path, invalid_locks[index]);
    }

    passed = passed &&
             verify_path_rejected(lock_path, directory, "/etc/passwd") &&
             verify_path_rejected(lock_path, directory,
                                  "https://example.invalid/dep") &&
             verify_path_rejected(lock_path, directory, "missing") &&
             verify_path_rejected(lock_path, directory, escape_path);

    passed = passed && !telos_plugin_lock_verify(NULL, directory, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_plugin_source_digest(NULL, digest, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_plugin_source_digest(directory, NULL, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        !telos_plugin_source_digest("/missing/telos-source", digest, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed &&
             !telos_plugin_lock_verify_source(NULL, directory, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    write_text(lock_path, "format = 1\nsource_hash = \"" ZERO_HASH
                          "\"\ndependencies = []\n");
    lock = telos_plugin_lock_load(lock_path, NULL);
    passed = passed && lock != NULL &&
             strcmp(telos_plugin_lock_source_hash(lock), ZERO_HASH) == 0 &&
             telos_plugin_lock_dependency_count(lock) == 0 &&
             telos_plugin_lock_dependency_at(lock, 0) == NULL &&
             !telos_plugin_lock_verify_source(lock, "/missing/telos-source",
                                              &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_plugin_lock_write(NULL, lock_path, &error) &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed && !telos_plugin_lock_write(lock, "", &error) && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && !telos_plugin_lock_write(lock, directory, &error) &&
             error != NULL;
    telos_error_release(error);
    telos_plugin_lock_destroy(lock);
    telos_plugin_lock_destroy(NULL);
    return passed && telos_plugin_lock_source_hash(NULL) == NULL &&
           telos_plugin_lock_dependency_count(NULL) == 0 &&
           telos_plugin_lock_dependency_at(NULL, 0) == NULL;
}

int main(void)
{
    char directory[] = "/tmp/telos-manifest-invalid-XXXXXX";
    char manifest_path[512];
    char lock_path[512];
    char outside_path[512];
    char escape_path[512];
    char oversized_path[512];
    struct telos_error *error = NULL;
    FILE *file;
    bool passed;

    if (mkdtemp(directory) == NULL ||
        snprintf(manifest_path, sizeof(manifest_path), "%s/plugin.toml",
                 directory) < 0 ||
        snprintf(lock_path, sizeof(lock_path), "%s/lock.toml", directory) < 0 ||
        snprintf(outside_path, sizeof(outside_path), "%s.outside", directory) <
            0 ||
        snprintf(escape_path, sizeof(escape_path), "%s/escape-link",
                 directory) < 0 ||
        snprintf(oversized_path, sizeof(oversized_path), "%s/oversized.toml",
                 directory) < 0) {
        return 1;
    }
    passed = write_text(outside_path, "outside") &&
             symlink(outside_path, escape_path) == 0 &&
             telos_plugin_manifest_load(NULL, &error) == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && telos_plugin_manifest_load("", &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        telos_plugin_manifest_load("/missing/plugin.toml", &error) == NULL &&
        error != NULL;
    telos_error_release(error);
    error = NULL;

    file = fopen(oversized_path, "wb");
    for (size_t index = 0; file != NULL && index <= 1024U * 1024U; ++index) {
        if (fputc('x', file) == EOF) {
            break;
        }
    }
    passed = passed && file != NULL && fclose(file) == 0 &&
             telos_plugin_manifest_load(oversized_path, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    passed = passed && manifest_matrix(manifest_path) &&
             lock_matrix(lock_path, directory, "escape-link") &&
             telos_plugin_manifest_id(NULL) == NULL &&
             telos_plugin_manifest_name(NULL) == NULL &&
             telos_plugin_manifest_version(NULL) == NULL &&
             telos_plugin_manifest_abi(NULL) == 0 &&
             telos_plugin_manifest_runtime_modes(NULL) == 0 &&
             telos_plugin_manifest_default_runtime(NULL) == 0 &&
             telos_plugin_manifest_permission_count(NULL) == 0 &&
             telos_plugin_manifest_permission_at(NULL, 0) == NULL;

    telos_plugin_manifest_destroy(NULL);
    unlink(escape_path);
    unlink(oversized_path);
    unlink(lock_path);
    unlink(manifest_path);
    unlink(outside_path);
    rmdir(directory);
    if (!passed) {
        fputs("Manifest and lock validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
