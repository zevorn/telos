#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/config.h>
#include <telos/install.h>
#include <telos/manifest.h>
#include <telos/resource.h>
#include <telos/store.h>
#include <telos/trace.h>
#include <telos/value.h>

#define TELOS_VERSION "0.1.0"

struct cli_options {
    bool json;
    bool yes;
    const char *state_directory;
    const char *builder;
    const char *sdk_pkgconfig;
    const char *sdk_sysroot;
    const char *abi_check;
    const char *plugin_host;
    const char *container_image;
};

static void usage(FILE *stream)
{
    fputs(
        "Telos Agentic Framework 0.1.0\n"
        "usage: telos [OPTIONS] COMMAND [ARGS]\n"
        "\n"
        "Commands:\n"
        "  run\n"
        "  chat\n"
        "  doctor\n"
        "  plugin list|info|install|build|test|activate|rollback|remove\n"
        "  resource list|validate|reload\n"
        "  state inspect|trace\n"
        "\n"
        "Options:\n"
        "  --json                 Emit machine-readable JSON\n"
        "  --yes                  Approve an inspected installation plan\n"
        "  --state-dir PATH       Override state.directory\n"
        "  --builder BACKEND      native or container\n"
        "  --sdk-pkgconfig PATH   SDK pkg-config directory\n"
        "  --sdk-sysroot PATH     SDK installation sysroot\n"
        "  --abi-check PATH       telos-abi-check executable\n"
        "  --plugin-host PATH     telos-plugin-host executable\n"
        "  --help                  Show this help\n",
        stream
    );
}

static int print_error(
    bool json,
    int exit_code,
    const struct telos_error *error,
    const char *fallback
)
{
    const char *message = error == NULL
        ? fallback
        : telos_error_message(error);

    if (!json) {
        fprintf(stderr, "telos: %s\n", message);
        return exit_code;
    }
    {
        struct telos_value *ok = telos_value_new_boolean(false);
        struct telos_value *domain = telos_value_new_integer(
            error == NULL ? 0 : telos_error_domain(error)
        );
        struct telos_value *code = telos_value_new_integer(
            error == NULL ? exit_code : telos_error_code(error)
        );
        struct telos_value *message_value = telos_value_new_string(message);
        const char *error_keys[] = {"domain", "code", "message"};
        const struct telos_value *error_values[] = {
            domain,
            code,
            message_value,
        };
        struct telos_value *error_value = telos_value_new_object(
            error_keys,
            error_values,
            3
        );
        const char *keys[] = {"ok", "error"};
        const struct telos_value *values[] = {ok, error_value};
        struct telos_value *root = telos_value_new_object(keys, values, 2);
        size_t size = telos_value_json_size(root);
        char *output = malloc(size);

        if (
            output != NULL
            && telos_value_write_json(
                root,
                output,
                size,
                NULL,
                NULL
            )
        ) {
            puts(output);
        }
        free(output);
        telos_value_release(root);
        telos_value_release(error_value);
        telos_value_release(message_value);
        telos_value_release(code);
        telos_value_release(domain);
        telos_value_release(ok);
    }
    return exit_code;
}

static bool print_json(const struct telos_value *value)
{
    size_t size = telos_value_json_size(value);
    char *output = malloc(size);
    bool result = output != NULL
        && telos_value_write_json(value, output, size, NULL, NULL);

    if (result) {
        puts(output);
    }
    free(output);
    return result;
}

static bool approve_install(
    const struct telos_install_risk *risk,
    void *context
)
{
    const struct cli_options *options = context;

    if (options->yes) {
        return true;
    }
    fprintf(
        stderr,
        "Installation requires approval: git=%s native=%s "
        "unlocked-source=%s permissions=%zu. "
        "Re-run with --yes after review.\n",
        risk->git_source ? "true" : "false",
        risk->native_build ? "true" : "false",
        risk->unlocked_source ? "true" : "false",
        risk->permission_count
    );
    return false;
}

static const char *state_name(enum telos_install_state state)
{
    static const char *names[] = {
        "",
        "resolve",
        "fetch_to_quarantine",
        "inspect",
        "plan",
        "authorize",
        "verify_dependencies",
        "build",
        "test",
        "abi_check",
        "stage",
        "health_check",
        "activate",
        "commit",
        "rollback",
        "completed",
    };

    return state > 0 && state <= TELOS_INSTALL_COMPLETED
        ? names[state]
        : "unknown";
}

static void install_progress(enum telos_install_state state, void *context)
{
    const struct cli_options *options = context;

    if (!options->json) {
        fprintf(stderr, "%s\n", state_name(state));
    }
}

static int plugin_info(
    const struct cli_options *options,
    const char *path
)
{
    struct telos_plugin_manifest *manifest;
    struct telos_error *error = NULL;
    char manifest_path[4096];
    struct stat status;

    if (stat(path, &status) == 0 && S_ISDIR(status.st_mode)) {
        if (
            snprintf(
                manifest_path,
                sizeof(manifest_path),
                "%s/plugin.toml",
                path
            ) >= (int)sizeof(manifest_path)
        ) {
            return print_error(
                options->json,
                2,
                NULL,
                "Plugin path is too long"
            );
        }
        path = manifest_path;
    }
    manifest = telos_plugin_manifest_load(path, &error);
    if (manifest == NULL) {
        int result = print_error(
            options->json,
            3,
            error,
            "Plugin manifest is invalid"
        );

        telos_error_release(error);
        return result;
    }
    if (options->json) {
        struct telos_value *ok = telos_value_new_boolean(true);
        struct telos_value *id = telos_value_new_string(
            telos_plugin_manifest_id(manifest)
        );
        struct telos_value *name = telos_value_new_string(
            telos_plugin_manifest_name(manifest)
        );
        struct telos_value *version = telos_value_new_string(
            telos_plugin_manifest_version(manifest)
        );
        struct telos_value *abi = telos_value_new_integer(
            telos_plugin_manifest_abi(manifest)
        );
        const char *keys[] = {"ok", "id", "name", "version", "abi"};
        const struct telos_value *values[] = {ok, id, name, version, abi};
        struct telos_value *root = telos_value_new_object(keys, values, 5);

        print_json(root);
        telos_value_release(root);
        telos_value_release(abi);
        telos_value_release(version);
        telos_value_release(name);
        telos_value_release(id);
        telos_value_release(ok);
    } else {
        printf(
            "%s %s (%s, ABI %u)\n",
            telos_plugin_manifest_id(manifest),
            telos_plugin_manifest_version(manifest),
            telos_plugin_manifest_name(manifest),
            telos_plugin_manifest_abi(manifest)
        );
    }
    telos_plugin_manifest_destroy(manifest);
    return 0;
}

static bool ensure_state_directory(
    const char *path,
    struct telos_error **error
)
{
    struct stat status;

    if (mkdir(path, 0700) != 0 && errno != EEXIST) {
        if (error != NULL) {
            *error = telos_error_create(
                TELOS_ERROR_DOMAIN_IO,
                errno,
                "State directory could not be created",
                NULL
            );
        }
        return false;
    }
    if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
        if (error != NULL) {
            *error = telos_error_create(
                TELOS_ERROR_DOMAIN_IO,
                ENOTDIR,
                "State path is not a directory",
                NULL
            );
        }
        return false;
    }
    return true;
}

static int plugin_prepare(
    const struct cli_options *options,
    const char *source,
    enum telos_install_goal goal
)
{
    struct telos_install_options install_options = {
        .source = source,
        .state_directory = options->state_directory,
        .sdk_pkgconfig_path = options->sdk_pkgconfig,
        .sdk_sysroot = options->sdk_sysroot,
        .abi_check_path = options->abi_check,
        .plugin_host_path = options->plugin_host,
        .container_image = options->container_image,
        .builder = strcmp(options->builder, "native") == 0
            ? TELOS_BUILDER_NATIVE
            : TELOS_BUILDER_CONTAINER,
        .goal = goal,
        .timeout_seconds = 300,
        .approve = approve_install,
        .approve_context = (void *)options,
        .progress = install_progress,
        .progress_context = (void *)options,
    };
    struct telos_install_result installed = {0};
    struct telos_error *error = NULL;

    if (
        !ensure_state_directory(options->state_directory, &error)
        || !telos_plugin_install(
            &install_options,
            NULL,
            &installed,
            &error
        )
    ) {
        int result = print_error(
            options->json,
            telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION
                ? 4
                : 3,
            error,
            goal == TELOS_INSTALL_GOAL_INSTALL
                ? "Plugin installation failed"
                : "Plugin preparation failed"
        );

        telos_error_release(error);
        telos_install_result_clear(&installed);
        return result;
    }
    if (options->json) {
        struct telos_value *ok = telos_value_new_boolean(true);
        struct telos_value *id = telos_value_new_string(installed.plugin_id);
        struct telos_value *version = telos_value_new_string(
            installed.version
        );
        struct telos_value *key = telos_value_new_string(installed.cache_key);
        struct telos_value *cache_hit = telos_value_new_boolean(
            installed.cache_hit
        );
        const char *keys[] = {
            "ok",
            "id",
            "version",
            "cache_key",
            "cache_hit",
        };
        const struct telos_value *values[] = {
            ok,
            id,
            version,
            key,
            cache_hit,
        };
        struct telos_value *root = telos_value_new_object(keys, values, 5);

        print_json(root);
        telos_value_release(root);
        telos_value_release(cache_hit);
        telos_value_release(key);
        telos_value_release(version);
        telos_value_release(id);
        telos_value_release(ok);
    } else {
        printf(
            "%s %s %s (%s)\n",
            goal == TELOS_INSTALL_GOAL_INSTALL ? "Installed" : "Prepared",
            installed.plugin_id,
            installed.version,
            installed.cache_hit ? "cache hit" : "built from source"
        );
    }
    telos_install_result_clear(&installed);
    return 0;
}

static int plugin_change(
    const struct cli_options *options,
    const char *plugin_id,
    const char *cache_key,
    bool remove
)
{
    struct telos_error *error = NULL;
    bool changed = remove
        ? telos_plugin_remove(
            options->state_directory,
            plugin_id,
            &error
        )
        : telos_plugin_activate(
            options->state_directory,
            plugin_id,
            cache_key,
            &error
        );

    if (!changed) {
        int result = print_error(
            options->json,
            3,
            error,
            remove ? "Plugin removal failed" : "Plugin activation failed"
        );

        telos_error_release(error);
        return result;
    }
    if (options->json) {
        struct telos_value *ok = telos_value_new_boolean(true);
        struct telos_value *id = telos_value_new_string(plugin_id);
        struct telos_value *action = telos_value_new_string(
            remove ? "removed" : "activated"
        );
        const char *keys[] = {"ok", "id", "action"};
        const struct telos_value *values[] = {ok, id, action};
        struct telos_value *root = telos_value_new_object(keys, values, 3);

        print_json(root);
        telos_value_release(root);
        telos_value_release(action);
        telos_value_release(id);
        telos_value_release(ok);
    } else {
        printf(
            "%s %s\n",
            remove ? "Removed" : "Activated",
            plugin_id
        );
    }
    return 0;
}

static int plugin_rollback(
    const struct cli_options *options,
    const char *plugin_id
)
{
    struct telos_error *error = NULL;

    if (!telos_plugin_rollback(
        options->state_directory,
        plugin_id,
        &error
    )) {
        int result = print_error(
            options->json,
            3,
            error,
            "Plugin rollback failed"
        );

        telos_error_release(error);
        return result;
    }
    if (options->json) {
        printf("{\"ok\":true,\"id\":\"%s\",\"rolled_back\":true}\n", plugin_id);
    } else {
        printf("Rolled back %s\n", plugin_id);
    }
    return 0;
}

static int plugin_list(const struct cli_options *options)
{
    char path[4096];
    DIR *directory;
    struct dirent *entry;
    bool first = true;

    if (
        snprintf(
            path,
            sizeof(path),
            "%s/plugins",
            options->state_directory
        ) >= (int)sizeof(path)
    ) {
        return print_error(options->json, 2, NULL, "State path is too long");
    }
    directory = opendir(path);
    if (directory == NULL && errno == ENOENT) {
        puts(options->json ? "{\"ok\":true,\"plugins\":[]}" : "No Plugins");
        return 0;
    }
    if (directory == NULL) {
        return print_error(
            options->json,
            3,
            NULL,
            "Plugin directory is inaccessible"
        );
    }
    if (options->json) {
        fputs("{\"ok\":true,\"plugins\":[", stdout);
    }
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (options->json) {
            printf("%s\"%s\"", first ? "" : ",", entry->d_name);
        } else {
            puts(entry->d_name);
        }
        first = false;
    }
    closedir(directory);
    if (options->json) {
        puts("]}");
    } else if (first) {
        puts("No Plugins");
    }
    return 0;
}

static int resource_validate(
    const struct cli_options *options,
    const char *root
)
{
    const char *roots[] = {root};
    struct telos_error *error = NULL;
    struct telos_resource_manager *manager = telos_resource_manager_create(
        roots,
        1,
        &error
    );
    struct telos_resource_generation *generation;
    size_t count;

    if (
        manager == NULL
        || !telos_resource_manager_reload(manager, &error)
    ) {
        int result = print_error(
            options->json,
            3,
            error,
            "Resource validation failed"
        );

        telos_error_release(error);
        telos_resource_manager_destroy(manager);
        return result;
    }
    generation = telos_resource_manager_acquire(manager);
    count = telos_resource_generation_skill_count(generation);
    if (options->json) {
        printf("{\"ok\":true,\"skills\":%zu}\n", count);
    } else {
        printf("Valid Resources: %zu Skills\n", count);
    }
    telos_resource_generation_release(generation);
    telos_resource_manager_destroy(manager);
    return 0;
}

static int state_trace(
    const struct cli_options *options,
    const char *path,
    bool inspect_only
)
{
    struct telos_error *error = NULL;
    struct telos_event_store *store = telos_markdown_store_create(path, &error);
    size_t count;

    if (store == NULL) {
        int result = print_error(
            options->json,
            3,
            error,
            "State file could not be opened"
        );

        telos_error_release(error);
        return result;
    }
    count = telos_event_store_count(store);
    if (inspect_only) {
        if (options->json) {
            printf("{\"ok\":true,\"events\":%zu}\n", count);
        } else {
            printf("%zu Events\n", count);
        }
    } else {
        for (size_t index = 0; index < count; ++index) {
            struct telos_event *event = telos_event_store_get(
                store,
                index,
                &error
            );
            size_t size = telos_event_trace_json_size(event);
            char *output = malloc(size);

            if (
                event == NULL
                || output == NULL
                || !telos_event_write_trace_json(
                    event,
                    output,
                    size,
                    NULL,
                    &error
                )
            ) {
                free(output);
                telos_event_release(event);
                telos_event_store_destroy(store);
                {
                    int result = print_error(
                        options->json,
                        3,
                        error,
                        "State trace failed"
                    );

                    telos_error_release(error);
                    return result;
                }
            }
            puts(output);
            free(output);
            telos_event_release(event);
        }
    }
    telos_event_store_destroy(store);
    return 0;
}

static int doctor(
    const struct cli_options *options,
    const struct telos_config *config
)
{
    if (options->json) {
        struct telos_value *ok = telos_value_new_boolean(true);
        struct telos_value *version = telos_value_new_string(TELOS_VERSION);
        struct telos_value *provider = telos_value_new_string(
            telos_config_get(config, "agent.provider")
        );
        struct telos_value *builder = telos_value_new_string(options->builder);
        struct telos_value *state = telos_value_new_string(
            options->state_directory
        );
        const char *keys[] = {
            "ok",
            "version",
            "provider",
            "builder",
            "state_directory",
        };
        const struct telos_value *values[] = {
            ok,
            version,
            provider,
            builder,
            state,
        };
        struct telos_value *root = telos_value_new_object(keys, values, 5);

        print_json(root);
        telos_value_release(root);
        telos_value_release(state);
        telos_value_release(builder);
        telos_value_release(provider);
        telos_value_release(version);
        telos_value_release(ok);
    } else {
        printf(
            "Telos %s\nProvider: %s\nBuilder: %s\nState: %s\n",
            TELOS_VERSION,
            telos_config_get(config, "agent.provider"),
            options->builder,
            options->state_directory
        );
    }
    return 0;
}

static int unsupported(
    const struct cli_options *options,
    const char *message
)
{
    return print_error(options->json, 4, NULL, message);
}

static int dispatch(
    int argc,
    char **argv,
    int index,
    const struct cli_options *options,
    const struct telos_config *config
)
{
    const char *command = argv[index++];

    if (strcmp(command, "doctor") == 0 && index == argc) {
        return doctor(options, config);
    }
    if (
        (strcmp(command, "run") == 0 || strcmp(command, "chat") == 0)
        && index == argc
    ) {
        return unsupported(
            options,
            "A configured Provider Plugin is required"
        );
    }
    if (strcmp(command, "plugin") == 0 && index < argc) {
        const char *subcommand = argv[index++];

        if (strcmp(subcommand, "list") == 0 && index == argc) {
            return plugin_list(options);
        }
        if (strcmp(subcommand, "info") == 0 && index + 1 == argc) {
            return plugin_info(options, argv[index]);
        }
        if (strcmp(subcommand, "install") == 0 && index + 1 == argc) {
            return plugin_prepare(
                options,
                argv[index],
                TELOS_INSTALL_GOAL_INSTALL
            );
        }
        if (strcmp(subcommand, "build") == 0 && index + 1 == argc) {
            return plugin_prepare(
                options,
                argv[index],
                TELOS_INSTALL_GOAL_BUILD
            );
        }
        if (strcmp(subcommand, "test") == 0 && index + 1 == argc) {
            return plugin_prepare(
                options,
                argv[index],
                TELOS_INSTALL_GOAL_TEST
            );
        }
        if (strcmp(subcommand, "activate") == 0 && index + 2 == argc) {
            return plugin_change(
                options,
                argv[index],
                argv[index + 1],
                false
            );
        }
        if (strcmp(subcommand, "remove") == 0 && index + 1 == argc) {
            return plugin_change(options, argv[index], NULL, true);
        }
        if (strcmp(subcommand, "rollback") == 0 && index + 1 == argc) {
            return plugin_rollback(options, argv[index]);
        }
    }
    if (strcmp(command, "resource") == 0 && index < argc) {
        const char *subcommand = argv[index++];

        if (
            (
                strcmp(subcommand, "validate") == 0
                || strcmp(subcommand, "list") == 0
                || strcmp(subcommand, "reload") == 0
            )
            && index + 1 == argc
        ) {
            return resource_validate(options, argv[index]);
        }
    }
    if (strcmp(command, "state") == 0 && index < argc) {
        const char *subcommand = argv[index++];

        if (strcmp(subcommand, "trace") == 0 && index + 1 == argc) {
            return state_trace(options, argv[index], false);
        }
        if (strcmp(subcommand, "inspect") == 0 && index + 1 == argc) {
            return state_trace(options, argv[index], true);
        }
    }
    return print_error(options->json, 2, NULL, "Invalid command or arguments");
}

int main(int argc, char **argv)
{
    struct cli_options options = {
        .sdk_pkgconfig = "",
        .sdk_sysroot = "/",
        .abi_check = "telos-abi-check",
        .plugin_host = "telos-plugin-host",
        .container_image = "ghcr.io/processmission/telos-sdk-c:0.1.0",
    };
    const char *home = getenv("HOME");
    char current[4096];
    struct telos_config *config;
    struct telos_error *error = NULL;
    int index = 1;
    int result;

    if (argc == 1) {
        usage(stdout);
        return 0;
    }
    if (home == NULL || getcwd(current, sizeof(current)) == NULL) {
        return print_error(false, 3, NULL, "HOME or current directory missing");
    }
    config = telos_config_load(home, current, &error);
    if (config == NULL) {
        result = print_error(false, 3, error, "Configuration load failed");
        telos_error_release(error);
        return result;
    }
    while (index < argc && strncmp(argv[index], "--", 2) == 0) {
        if (strcmp(argv[index], "--help") == 0) {
            usage(stdout);
            telos_config_destroy(config);
            return 0;
        }
        if (strcmp(argv[index], "--json") == 0) {
            options.json = true;
            index += 1;
        } else if (strcmp(argv[index], "--yes") == 0) {
            options.yes = true;
            index += 1;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--state-dir") == 0
        ) {
            if (!telos_config_override(
                config,
                "state.directory",
                argv[index + 1],
                &error
            )) {
                break;
            }
            index += 2;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--builder") == 0
        ) {
            if (!telos_config_override(
                config,
                "builder.backend",
                argv[index + 1],
                &error
            )) {
                break;
            }
            index += 2;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--sdk-pkgconfig") == 0
        ) {
            options.sdk_pkgconfig = argv[index + 1];
            index += 2;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--sdk-sysroot") == 0
        ) {
            options.sdk_sysroot = argv[index + 1];
            index += 2;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--abi-check") == 0
        ) {
            options.abi_check = argv[index + 1];
            index += 2;
        } else if (
            index + 1 < argc
            && strcmp(argv[index], "--plugin-host") == 0
        ) {
            options.plugin_host = argv[index + 1];
            index += 2;
        } else {
            break;
        }
    }
    if (error != NULL) {
        result = print_error(
            options.json,
            2,
            error,
            "Invalid command-line option"
        );
        telos_error_release(error);
        telos_config_destroy(config);
        return result;
    }
    options.state_directory = telos_config_get(config, "state.directory");
    options.builder = telos_config_get(config, "builder.backend");
    if (index >= argc) {
        result = print_error(
            options.json,
            2,
            NULL,
            "A command is required"
        );
    } else {
        result = dispatch(argc, argv, index, &options, config);
    }
    telos_config_destroy(config);
    return result;
}
