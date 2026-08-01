#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/install.h>

#define KEY_ZERO                                                               \
    "0000000000000000000000000000000000000000000000000000000000000000"
#define KEY_ONE                                                                \
    "1111111111111111111111111111111111111111111111111111111111111111"

static bool make_directory(const char *path)
{
    return mkdir(path, 0700) == 0;
}

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

static bool install_rejected(const struct telos_install_options *options,
                             const struct telos_cancel *cancel)
{
    struct telos_install_result result = {
        .plugin_id = (char *)1,
        .cache_hit = true,
    };
    struct telos_error *error = NULL;
    bool rejected = !telos_plugin_install(options, cancel, &result, &error) &&
                    error != NULL && result.plugin_id == NULL &&
                    !result.cache_hit;

    telos_error_release(error);
    telos_install_result_clear(&result);
    return rejected;
}

static bool
activate_rejected(const char *state, const char *plugin_id, const char *key)
{
    struct telos_error *error = NULL;
    bool rejected =
        !telos_plugin_activate(state, plugin_id, key, &error) && error != NULL;

    telos_error_release(error);
    return rejected;
}

static bool remove_rejected(const char *state, const char *plugin_id)
{
    struct telos_error *error = NULL;
    bool rejected =
        !telos_plugin_remove(state, plugin_id, &error) && error != NULL;

    telos_error_release(error);
    return rejected;
}

static bool rollback_rejected(const char *state, const char *plugin_id)
{
    struct telos_error *error = NULL;
    bool rejected =
        !telos_plugin_rollback(state, plugin_id, &error) && error != NULL;

    telos_error_release(error);
    return rejected;
}

int main(void)
{
    char root[] = "/tmp/telos-install-invalid-XXXXXX";
    char state[512];
    char source[512];
    char cache[512];
    char cache_plugins[512];
    char zero[512];
    char zero_staged[512];
    char zero_nested[512];
    char zero_artifact[512];
    char one[512];
    char one_staged[512];
    char one_artifact[512];
    char plugins[512];
    char removable[512];
    char removable_nested[512];
    char removable_file[512];
    char removable_link[512];
    struct telos_install_options valid;
    struct telos_install_options invalid;
    struct telos_cancel *cancel;
    struct telos_error *error = NULL;
    bool passed;

    if (mkdtemp(root) == NULL ||
        snprintf(state, sizeof(state), "%s/state", root) < 0 ||
        snprintf(source, sizeof(source), "%s/source", root) < 0 ||
        snprintf(cache, sizeof(cache), "%s/cache", state) < 0 ||
        snprintf(cache_plugins, sizeof(cache_plugins), "%s/plugins", cache) <
            0 ||
        snprintf(zero, sizeof(zero), "%s/plugins/%s", cache, KEY_ZERO) < 0 ||
        snprintf(zero_staged, sizeof(zero_staged), "%s/staged", zero) < 0 ||
        snprintf(zero_nested, sizeof(zero_nested), "%s/deep", zero_staged) <
            0 ||
        snprintf(zero_artifact, sizeof(zero_artifact), "%s/plugin.so",
                 zero_nested) < 0 ||
        snprintf(one, sizeof(one), "%s/plugins/%s", cache, KEY_ONE) < 0 ||
        snprintf(one_staged, sizeof(one_staged), "%s/staged", one) < 0 ||
        snprintf(one_artifact, sizeof(one_artifact), "%s/plugin.so",
                 one_staged) < 0 ||
        snprintf(plugins, sizeof(plugins), "%s/plugins", state) < 0 ||
        snprintf(removable, sizeof(removable), "%s/remove-me", plugins) < 0 ||
        snprintf(removable_nested, sizeof(removable_nested), "%s/nested",
                 removable) < 0 ||
        snprintf(removable_file, sizeof(removable_file), "%s/file",
                 removable_nested) < 0 ||
        snprintf(removable_link, sizeof(removable_link), "%s/link", removable) <
            0 ||
        !make_directory(state) || !make_directory(source)) {
        return 1;
    }

    valid = (struct telos_install_options){
        .source = source,
        .state_directory = state,
        .sdk_pkgconfig_path = "/tmp",
        .sdk_sysroot = "/tmp",
        .abi_check_path = "/bin/false",
        .plugin_host_path = "/bin/false",
        .builder = TELOS_BUILDER_NATIVE,
        .goal = TELOS_INSTALL_GOAL_INSTALL,
        .timeout_seconds = 1,
    };
    passed = install_rejected(NULL, NULL);
    invalid = valid;
    invalid.source = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid.source = "";
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.state_directory = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid.state_directory = "/missing/telos-state";
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.sdk_pkgconfig_path = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.sdk_sysroot = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.abi_check_path = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.plugin_host_path = NULL;
    passed = passed && install_rejected(&invalid, NULL);
    invalid.plugin_host_path = "";
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.builder = 0;
    passed = passed && install_rejected(&invalid, NULL);
    invalid.builder = 99;
    passed = passed && install_rejected(&invalid, NULL);
    invalid = valid;
    invalid.goal = (enum telos_install_goal)-1;
    passed = passed && install_rejected(&invalid, NULL);
    invalid.goal = (enum telos_install_goal)99;
    passed = passed && install_rejected(&invalid, NULL);

    cancel = telos_cancel_create();
    telos_cancel_request(cancel);
    passed = passed && install_rejected(&valid, cancel);
    telos_cancel_release(cancel);
    invalid = valid;
    invalid.source = "/missing/telos-plugin";
    passed = passed && install_rejected(&invalid, NULL);
    invalid.source = "local:/missing/telos-plugin";
    passed = passed && install_rejected(&invalid, NULL);
    invalid.source = "git:/missing/telos-plugin";
    passed = passed && install_rejected(&invalid, NULL);

    passed = passed && activate_rejected(NULL, "plugin", KEY_ZERO) &&
             activate_rejected(state, NULL, KEY_ZERO) &&
             activate_rejected(state, "", KEY_ZERO) &&
             activate_rejected(state, "bad/id", KEY_ZERO) &&
             activate_rejected(state, "plugin", NULL) &&
             activate_rejected(state, "plugin", "short") &&
             activate_rejected(state, "plugin",
                               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                               "AAAAAAAAAAAAAAAAA") &&
             activate_rejected(state, "plugin", KEY_ZERO);

    passed = passed && make_directory(cache) && make_directory(cache_plugins);
    passed = passed && make_directory(zero) && make_directory(zero_staged) &&
             make_directory(zero_nested) &&
             write_text(zero_artifact, "fixture") && make_directory(one) &&
             make_directory(one_staged) &&
             write_text(one_artifact, "fixture") &&
             telos_plugin_activate(state, "plugin", KEY_ZERO, &error) &&
             error == NULL &&
             telos_plugin_activate(state, "plugin", KEY_ONE, &error) &&
             error == NULL && telos_plugin_rollback(state, "plugin", &error) &&
             error == NULL;

    passed = passed && rollback_rejected(NULL, "plugin") &&
             rollback_rejected(state, NULL) &&
             rollback_rejected(state, "bad/id") &&
             rollback_rejected(state, "missing") &&
             remove_rejected(NULL, "plugin") && remove_rejected(state, NULL) &&
             remove_rejected(state, "") && remove_rejected(state, "bad/id") &&
             remove_rejected(state, "missing");

    passed = passed && make_directory(removable) &&
             make_directory(removable_nested) &&
             write_text(removable_file, "fixture") &&
             symlink(removable_file, removable_link) == 0 &&
             telos_plugin_remove(state, "remove-me", &error) && error == NULL &&
             access(removable, F_OK) != 0 &&
             telos_plugin_remove(state, "plugin", &error) && error == NULL;

    {
        struct telos_install_result result = {
            .plugin_id = strdup("plugin"),
            .version = strdup("1"),
            .cache_key = strdup(KEY_ZERO),
            .artifact_path = strdup(zero_artifact),
            .cache_hit = true,
        };

        telos_install_result_clear(&result);
        passed = passed && result.plugin_id == NULL && result.version == NULL &&
                 result.cache_key == NULL && result.artifact_path == NULL &&
                 !result.cache_hit;
    }
    telos_install_result_clear(NULL);

    unlink(one_artifact);
    rmdir(one_staged);
    rmdir(one);
    unlink(zero_artifact);
    rmdir(zero_nested);
    rmdir(zero_staged);
    rmdir(zero);
    rmdir(cache_plugins);
    rmdir(cache);
    rmdir(source);
    rmdir(plugins);
    rmdir(state);
    rmdir(root);
    if (!passed) {
        fputs("Plugin installer public validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
