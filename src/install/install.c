#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <telos/install.h>
#include <telos/manifest.h>

#include "sha256.h"

#define PATH_BUFFER_SIZE 4096

struct process_environment {
    const char *pkgconfig_path;
    const char *sdk_sysroot;
    const char *destination;
};

static bool executable_prefix(const char *name,
                              const char *path,
                              char *prefix,
                              size_t prefix_size)
{
    const char *cursor = path;

    while (cursor != NULL && *cursor != '\0') {
        const char *end = strchr(cursor, ':');
        size_t directory_size =
            end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        char candidate[PATH_BUFFER_SIZE];
        char resolved[PATH_BUFFER_SIZE];
        char *bin;

        if (directory_size > 0 &&
            snprintf(candidate, sizeof(candidate), "%.*s/%s",
                     (int)directory_size, cursor,
                     name) < (int)sizeof(candidate) &&
            access(candidate, X_OK) == 0 &&
            realpath(candidate, resolved) != NULL) {
            bin = strrchr(resolved, '/');
            if (bin != NULL) {
                *bin = '\0';
                bin = strrchr(resolved, '/');
                if (bin != NULL && strcmp(bin + 1, "bin") == 0 &&
                    (size_t)(bin - resolved) + 1 <= prefix_size) {
                    *bin = '\0';
                    memcpy(prefix, resolved, strlen(resolved) + 1);
                    return true;
                }
            }
        }
        cursor = end == NULL ? NULL : end + 1;
    }
    return false;
}

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static char *copy_text(const char *text)
{
    size_t size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }
    size = strlen(text) + 1;
    copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

static void progress(const struct telos_install_options *options,
                     enum telos_install_state state)
{
    if (options->progress != NULL) {
        options->progress(state, options->progress_context);
    }
}

static bool path_join(char *output,
                      size_t output_size,
                      const char *first,
                      const char *second)
{
    int written;

    if (output == NULL || first == NULL || second == NULL || first[0] == '\0') {
        return false;
    }
    written = snprintf(output, output_size, "%s/%s", first, second);
    return written >= 0 && (size_t)written < output_size;
}

static bool
make_directories(const char *path, mode_t mode, struct telos_error **error)
{
    char copy[PATH_BUFFER_SIZE];

    if (path == NULL || path[0] != '/' || strlen(path) + 1 > sizeof(copy)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Installation directory must be an absolute path");
        return false;
    }
    memcpy(copy, path, strlen(path) + 1);
    for (char *cursor = copy + 1;; ++cursor) {
        if (*cursor == '/' || *cursor == '\0') {
            char saved = *cursor;

            *cursor = '\0';
            if (mkdir(copy, mode) != 0 && errno != EEXIST) {
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "Installation directory could not be created");
                return false;
            }
            *cursor = saved;
            if (saved == '\0') {
                break;
            }
        }
    }
    return true;
}

static bool remove_tree(const char *path)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;
    bool result = true;

    if (path == NULL || path[0] != '/' || lstat(path, &status) != 0) {
        return errno == ENOENT;
    }
    if (!S_ISDIR(status.st_mode) || S_ISLNK(status.st_mode)) {
        return unlink(path) == 0;
    }
    directory = opendir(path);
    if (directory == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[PATH_BUFFER_SIZE];

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!path_join(child, sizeof(child), path, entry->d_name) ||
            !remove_tree(child)) {
            result = false;
            break;
        }
    }
    closedir(directory);
    return result && rmdir(path) == 0;
}

static bool run_process(const char *working_directory,
                        const char *log_path,
                        const struct process_environment *environment,
                        const char *const *arguments,
                        unsigned int timeout_seconds,
                        const struct telos_cancel *cancel,
                        struct telos_error **error)
{
    const char *host_path = getenv("PATH");
    char python_user_base[PATH_BUFFER_SIZE] = {0};
    pid_t child;
    time_t started;
    int status = 0;

    if (strcmp(arguments[0], "meson") == 0 && host_path != NULL) {
        executable_prefix(arguments[0], host_path, python_user_base,
                          sizeof(python_user_base));
    }
    child = fork();
    if (child < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Builder process could not be created");
        return false;
    }
    if (child == 0) {
        int log_descriptor;
        int null_descriptor;

        setpgid(0, 0);
        if (working_directory != NULL && chdir(working_directory) != 0) {
            _exit(125);
        }
        log_descriptor = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        null_descriptor = open("/dev/null", O_RDONLY);
        if (log_descriptor < 0 || null_descriptor < 0 ||
            dup2(null_descriptor, STDIN_FILENO) < 0 ||
            dup2(log_descriptor, STDOUT_FILENO) < 0 ||
            dup2(log_descriptor, STDERR_FILENO) < 0) {
            _exit(125);
        }
        close(null_descriptor);
        close(log_descriptor);
        clearenv();
        setenv("PATH", host_path == NULL ? "/usr/bin:/bin" : host_path, 1);
        setenv("HOME", "/nonexistent", 1);
        setenv("LC_ALL", "C", 1);
        setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
        setenv("CCACHE_DISABLE", "1", 1);
        if (python_user_base[0] != '\0') {
            setenv("PYTHONUSERBASE", python_user_base, 1);
        }
        if (environment != NULL && environment->pkgconfig_path != NULL) {
            setenv("PKG_CONFIG_PATH", environment->pkgconfig_path, 1);
        }
        if (environment != NULL && environment->sdk_sysroot != NULL) {
            setenv("PKG_CONFIG_SYSROOT_DIR", environment->sdk_sysroot, 1);
        }
        if (environment != NULL && environment->destination != NULL) {
            setenv("DESTDIR", environment->destination, 1);
        }
        execvp(arguments[0], (char *const *)arguments);
        _exit(127);
    }

    started = time(NULL);
    for (;;) {
        pid_t waited = waitpid(child, &status, WNOHANG);

        if (waited == child) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Builder process could not be observed");
            return false;
        }
        if (telos_cancel_requested(cancel)) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "Plugin installation was cancelled");
            return false;
        }
        if (timeout_seconds > 0 &&
            time(NULL) - started >= (time_t)timeout_seconds) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            set_error(error, TELOS_ERROR_DOMAIN_TIMEOUT, ETIMEDOUT,
                      "Plugin installation command timed out");
            return false;
        }
        {
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 10000000,
            };

            nanosleep(&pause, NULL);
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_PLUGIN,
                  WIFEXITED(status) ? WEXITSTATUS(status) : EIO,
                  "Plugin installation command failed; inspect build.log");
        return false;
    }
    return true;
}

static bool copy_directory(const char *source,
                           const char *destination,
                           const char *working_directory,
                           const char *log_path,
                           const struct telos_install_options *options,
                           const struct telos_cancel *cancel,
                           struct telos_error **error)
{
    char source_contents[PATH_BUFFER_SIZE];
    const char *arguments[] = {
        "cp", "-R", source_contents, destination, NULL,
    };

    if (snprintf(source_contents, sizeof(source_contents), "%s/.", source) >=
        (int)sizeof(source_contents)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Plugin source path is too long");
        return false;
    }
    return run_process(working_directory, log_path, NULL, arguments,
                       options->timeout_seconds, cancel, error);
}

static bool fetch_source(const struct telos_install_options *options,
                         const char *source_directory,
                         const char *quarantine,
                         const char *log_path,
                         bool git_source,
                         const struct telos_cancel *cancel,
                         struct telos_error **error)
{
    const char *source = options->source;

    if (git_source) {
        const char *arguments[] = {
            "git",      "clone",          "--quiet", "--no-hardlinks",
            source + 4, source_directory, NULL,
        };

        return run_process(quarantine, log_path, NULL, arguments,
                           options->timeout_seconds, cancel, error);
    }
    if (strncmp(source, "local:", 6) == 0) {
        source += 6;
    }
    if (!make_directories(source_directory, 0700, error)) {
        return false;
    }
    return copy_directory(source, source_directory, quarantine, log_path,
                          options, cancel, error);
}

static bool permission_contains(const struct telos_plugin_manifest *manifest,
                                const char *prefix)
{
    size_t prefix_size = strlen(prefix);

    for (size_t index = 0;
         index < telos_plugin_manifest_permission_count(manifest); ++index) {
        const char *permission =
            telos_plugin_manifest_permission_at(manifest, index);

        if (strncmp(permission, prefix, prefix_size) == 0) {
            return true;
        }
    }
    return false;
}

static struct telos_install_risk
inspect_risk(const struct telos_install_options *options,
             const struct telos_plugin_manifest *manifest,
             const struct telos_plugin_lock *lock,
             bool git_source)
{
    static const char unlocked_hash[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    struct telos_install_risk risk = {
        .git_source = git_source,
        .native_build = options->builder == TELOS_BUILDER_NATIVE,
        .network = permission_contains(manifest, "network."),
        .filesystem_write = permission_contains(manifest, "filesystem.write"),
        .process_spawn = permission_contains(manifest, "process.spawn"),
        .secret_use = permission_contains(manifest, "secret.use"),
        .unlocked_source =
            strcmp(telos_plugin_lock_source_hash(lock), unlocked_hash) == 0,
        .permission_count = telos_plugin_manifest_permission_count(manifest),
    };

    risk.requires_approval = risk.git_source || risk.native_build ||
                             risk.network || risk.filesystem_write ||
                             risk.process_spawn || risk.secret_use ||
                             risk.unlocked_source;
    return risk;
}

static bool write_build_inputs(const char *source_directory,
                               const struct telos_install_options *options,
                               char *path,
                               size_t path_size,
                               struct telos_error **error)
{
    FILE *stream;

    if (!path_join(path, path_size, source_directory, ".telos-build-input")) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Plugin build-input path is too long");
        return false;
    }
    stream = fopen(path, "wb");
    if (stream == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Plugin build inputs could not be recorded");
        return false;
    }
    if (fprintf(stream,
                "telos_sdk=0.1.0\nabi=1\ntarget=linux-x86_64\n"
                "builder=%u\npkgconfig=%s\n",
                (unsigned int)options->builder,
                options->sdk_pkgconfig_path) < 0 ||
        fclose(stream) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Plugin build inputs could not be written");
        return false;
    }
    return true;
}

static bool find_artifact_recursive(const char *directory_path,
                                    char *artifact,
                                    size_t artifact_size)
{
    DIR *directory = opendir(directory_path);
    struct dirent *entry;

    if (directory == NULL) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        char child[PATH_BUFFER_SIZE];
        struct stat status;
        size_t name_size;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            !path_join(child, sizeof(child), directory_path, entry->d_name) ||
            lstat(child, &status) != 0) {
            continue;
        }
        if (S_ISDIR(status.st_mode) &&
            find_artifact_recursive(child, artifact, artifact_size)) {
            closedir(directory);
            return true;
        }
        name_size = strlen(entry->d_name);
        if (S_ISREG(status.st_mode) && name_size > 3 &&
            strcmp(entry->d_name + name_size - 3, ".so") == 0 &&
            strlen(child) + 1 <= artifact_size) {
            memcpy(artifact, child, strlen(child) + 1);
            closedir(directory);
            return true;
        }
    }
    closedir(directory);
    return false;
}

static bool copy_file(const char *source,
                      const char *destination,
                      struct telos_error **error)
{
    FILE *input = fopen(source, "rb");
    FILE *output;
    unsigned char buffer[8192];
    size_t received;
    bool valid = true;
    bool input_failed;
    bool input_close_failed;
    bool output_close_failed;

    if (input == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Installation file could not be opened");
        return false;
    }
    output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Installation file could not be created");
        return false;
    }
    for (;;) {
        received = fread(buffer, 1, sizeof(buffer), input);
        if (received > 0 && fwrite(buffer, 1, received, output) != received) {
            valid = false;
            break;
        }
        if (ferror(input) != 0 || feof(input) != 0) {
            break;
        }
    }
    input_failed = ferror(input) != 0;
    input_close_failed = fclose(input) != 0;
    output_close_failed = fclose(output) != 0;
    if (input_failed || input_close_failed || output_close_failed) {
        valid = false;
    }
    if (!valid) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                  "Installation file copy failed");
    }
    return valid;
}

static bool create_cache_entry(const struct telos_install_options *options,
                               const struct telos_cancel *cancel,
                               const char *quarantine,
                               const char *source_directory,
                               const char *staging_directory,
                               const char *log_path,
                               const char *cache_entry,
                               struct telos_error **error)
{
    char entry[PATH_BUFFER_SIZE];
    char staged[PATH_BUFFER_SIZE];
    char source[PATH_BUFFER_SIZE];
    char cached_log[PATH_BUFFER_SIZE];

    if (!path_join(entry, sizeof(entry), quarantine, "cache-entry") ||
        !path_join(staged, sizeof(staged), entry, "staged") ||
        !path_join(source, sizeof(source), entry, "source") ||
        !path_join(cached_log, sizeof(cached_log), entry, "build.log") ||
        !make_directories(entry, 0700, error) ||
        !make_directories(staged, 0700, error) ||
        !make_directories(source, 0700, error) ||
        !copy_directory(staging_directory, staged, quarantine, log_path,
                        options, cancel, error) ||
        !copy_directory(source_directory, source, quarantine, log_path, options,
                        cancel, error) ||
        !copy_file(log_path, cached_log, error)) {
        return false;
    }
    if (rename(entry, cache_entry) != 0) {
        if (errno == EEXIST || errno == ENOTEMPTY) {
            return true;
        }
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Plugin cache entry could not be committed");
        return false;
    }
    return true;
}

static bool replace_link(const char *link_path,
                         const char *target,
                         struct telos_error **error)
{
    char temporary[PATH_BUFFER_SIZE];

    if (snprintf(temporary, sizeof(temporary), "%s.new.%ld", link_path,
                 (long)getpid()) >= (int)sizeof(temporary)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Plugin activation path is too long");
        return false;
    }
    unlink(temporary);
    if (symlink(target, temporary) != 0 || rename(temporary, link_path) != 0) {
        int saved = errno;

        unlink(temporary);
        set_error(error, TELOS_ERROR_DOMAIN_IO, saved,
                  "Plugin activation link could not be replaced");
        return false;
    }
    return true;
}

static bool activate_cache_entry(const char *state_directory,
                                 const char *plugin_id,
                                 const char *cache_entry,
                                 struct telos_error **error)
{
    char plugins[PATH_BUFFER_SIZE];
    char plugin_directory[PATH_BUFFER_SIZE];
    char current[PATH_BUFFER_SIZE];
    char previous[PATH_BUFFER_SIZE];
    char old_target[PATH_BUFFER_SIZE];
    ssize_t old_size;

    if (!path_join(plugins, sizeof(plugins), state_directory, "plugins") ||
        !path_join(plugin_directory, sizeof(plugin_directory), plugins,
                   plugin_id) ||
        !path_join(current, sizeof(current), plugin_directory, "current") ||
        !path_join(previous, sizeof(previous), plugin_directory, "previous") ||
        !make_directories(plugin_directory, 0700, error)) {
        return false;
    }
    old_size = readlink(current, old_target, sizeof(old_target) - 1);
    if (old_size >= 0) {
        old_target[old_size] = '\0';
        if (!replace_link(previous, old_target, error)) {
            return false;
        }
    } else if (errno != ENOENT) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Current Plugin activation could not be inspected");
        return false;
    }
    return replace_link(current, cache_entry, error);
}

static bool build_plugin(const struct telos_install_options *options,
                         const struct telos_cancel *cancel,
                         const char *source_directory,
                         const char *build_directory,
                         const char *staging_directory,
                         const char *log_path,
                         char *artifact,
                         size_t artifact_size,
                         struct telos_error **error)
{
    struct process_environment environment = {
        .pkgconfig_path = options->sdk_pkgconfig_path,
        .sdk_sysroot = options->sdk_sysroot,
    };
    const char *setup[] = {
        "meson",
        "setup",
        build_directory,
        source_directory,
        "--prefix=/",
        "--libdir=lib",
        "--buildtype=release",
        "--wrap-mode=nodownload",
        NULL,
    };
    const char *compile[] = {
        "meson", "compile", "-C", build_directory, NULL,
    };
    const char *test[] = {
        "meson", "test", "-C", build_directory, "--print-errorlogs", NULL,
    };
    const char *install[] = {
        "meson", "install", "-C", build_directory, NULL,
    };

    if (options->builder == TELOS_BUILDER_CONTAINER) {
        const char *runtime = NULL;
        char source_mount[PATH_BUFFER_SIZE];
        char build_mount[PATH_BUFFER_SIZE];
        char staging_mount[PATH_BUFFER_SIZE];
        char user[64];
        const char *arguments[40];
        size_t count = 0;
        const char *path = getenv("PATH");

        for (const char *candidate = "podman";; candidate = "docker") {
            const char *cursor = path;

            while (cursor != NULL && *cursor != '\0') {
                const char *end = strchr(cursor, ':');
                size_t directory_size =
                    end == NULL ? strlen(cursor) : (size_t)(end - cursor);
                char executable[PATH_BUFFER_SIZE];

                if (snprintf(executable, sizeof(executable), "%.*s/%s",
                             (int)directory_size, cursor,
                             candidate) < (int)sizeof(executable) &&
                    access(executable, X_OK) == 0) {
                    runtime = candidate;
                    break;
                }
                cursor = end == NULL ? NULL : end + 1;
            }
            if (runtime != NULL || strcmp(candidate, "docker") == 0) {
                break;
            }
        }
        if (runtime == NULL || options->container_image == NULL ||
            options->container_image[0] == '\0' ||
            !make_directories(build_directory, 0700, error) ||
            !make_directories(staging_directory, 0700, error) ||
            snprintf(source_mount, sizeof(source_mount), "%s:/workspace:ro",
                     source_directory) >= (int)sizeof(source_mount) ||
            snprintf(build_mount, sizeof(build_mount), "%s:/build:rw",
                     build_directory) >= (int)sizeof(build_mount) ||
            snprintf(staging_mount, sizeof(staging_mount), "%s:/staging:rw",
                     staging_directory) >= (int)sizeof(staging_mount) ||
            snprintf(user, sizeof(user), "%ld:%ld", (long)getuid(),
                     (long)getgid()) >= (int)sizeof(user)) {
            if (error == NULL || *error == NULL) {
                set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, ENOENT,
                          "Podman or Docker and a Builder image are required");
            }
            return false;
        }
        arguments[count++] = runtime;
        arguments[count++] = "run";
        arguments[count++] = "--rm";
        arguments[count++] = "--network=none";
        arguments[count++] = "--read-only";
        arguments[count++] = "--cap-drop=ALL";
        arguments[count++] = "--security-opt=no-new-privileges";
        arguments[count++] = "--pids-limit=256";
        arguments[count++] = "--memory=1g";
        arguments[count++] = "--cpus=2";
        arguments[count++] = "--tmpfs=/tmp:rw,noexec,nosuid,size=256m";
        if (strcmp(runtime, "podman") == 0) {
            arguments[count++] = "--userns=keep-id";
        } else {
            arguments[count++] = "--user";
            arguments[count++] = user;
        }
        arguments[count++] = "--volume";
        arguments[count++] = source_mount;
        arguments[count++] = "--volume";
        arguments[count++] = build_mount;
        arguments[count++] = "--volume";
        arguments[count++] = staging_mount;
        arguments[count++] = options->container_image;
        arguments[count++] = "--source";
        arguments[count++] = "/workspace";
        arguments[count++] = "--build-dir";
        arguments[count++] = "/build";
        arguments[count++] = "--staging";
        arguments[count++] = "/staging";
        arguments[count] = NULL;
        progress(options, TELOS_INSTALL_BUILD);
        progress(options, TELOS_INSTALL_TEST);
        progress(options, TELOS_INSTALL_STAGE);
        if (!run_process(source_directory, log_path, NULL, arguments,
                         options->timeout_seconds, cancel, error) ||
            !find_artifact_recursive(staging_directory, artifact,
                                     artifact_size)) {
            if (error == NULL || *error == NULL) {
                set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, ENOENT,
                          "Container Builder produced no shared module");
            }
            return false;
        }
        progress(options, TELOS_INSTALL_ABI_CHECK);
        {
            const char *abi_check[] = {
                options->abi_check_path,
                artifact,
                NULL,
            };

            if (!run_process(source_directory, log_path, NULL, abi_check,
                             options->timeout_seconds, cancel, error)) {
                return false;
            }
        }
        return true;
    }
    progress(options, TELOS_INSTALL_BUILD);
    if (!run_process(source_directory, log_path, &environment, setup,
                     options->timeout_seconds, cancel, error) ||
        !run_process(source_directory, log_path, &environment, compile,
                     options->timeout_seconds, cancel, error)) {
        return false;
    }
    progress(options, TELOS_INSTALL_TEST);
    if (!run_process(source_directory, log_path, &environment, test,
                     options->timeout_seconds, cancel, error)) {
        return false;
    }
    progress(options, TELOS_INSTALL_STAGE);
    environment.destination = staging_directory;
    if (!make_directories(staging_directory, 0700, error) ||
        !run_process(source_directory, log_path, &environment, install,
                     options->timeout_seconds, cancel, error) ||
        !find_artifact_recursive(staging_directory, artifact, artifact_size)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, ENOENT,
                      "Plugin build did not stage a shared module");
        }
        return false;
    }
    progress(options, TELOS_INSTALL_ABI_CHECK);
    {
        const char *abi_check[] = {
            options->abi_check_path,
            artifact,
            NULL,
        };

        if (!run_process(source_directory, log_path, &environment, abi_check,
                         options->timeout_seconds, cancel, error)) {
            return false;
        }
    }
    return true;
}

static bool health_check_plugin(const struct telos_install_options *options,
                                const struct telos_cancel *cancel,
                                const char *working_directory,
                                const char *log_path,
                                const char *artifact,
                                const char *plugin_id,
                                struct telos_error **error)
{
    const char *arguments[] = {
        options->plugin_host_path, "--health-check", artifact, plugin_id, NULL,
    };

    progress(options, TELOS_INSTALL_HEALTH_CHECK);
    return run_process(working_directory, log_path, NULL, arguments,
                       options->timeout_seconds, cancel, error);
}

bool telos_plugin_install(const struct telos_install_options *options,
                          const struct telos_cancel *cancel,
                          struct telos_install_result *result,
                          struct telos_error **error)
{
    char state_directory[PATH_BUFFER_SIZE];
    char quarantine_root[PATH_BUFFER_SIZE];
    char quarantine_template[PATH_BUFFER_SIZE];
    char *quarantine = NULL;
    char source_directory[PATH_BUFFER_SIZE];
    char manifest_path[PATH_BUFFER_SIZE];
    char lock_path[PATH_BUFFER_SIZE];
    char build_directory[PATH_BUFFER_SIZE];
    char staging_directory[PATH_BUFFER_SIZE];
    char log_path[PATH_BUFFER_SIZE];
    char build_input_path[PATH_BUFFER_SIZE] = {0};
    char cache_root[PATH_BUFFER_SIZE];
    char cache_entry[PATH_BUFFER_SIZE];
    char cached_staged[PATH_BUFFER_SIZE];
    char artifact[PATH_BUFFER_SIZE];
    char digest[65];
    struct telos_plugin_manifest *manifest = NULL;
    struct telos_plugin_lock *lock = NULL;
    struct telos_install_risk risk;
    struct stat status;
    bool git_source;
    bool cache_hit = false;
    bool success = false;

    if (error != NULL) {
        *error = NULL;
    }
    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (options == NULL || result == NULL || options->source == NULL ||
        options->source[0] == '\0' || options->state_directory == NULL ||
        options->sdk_pkgconfig_path == NULL || options->sdk_sysroot == NULL ||
        options->abi_check_path == NULL || options->plugin_host_path == NULL ||
        options->plugin_host_path[0] == '\0' ||
        (options->builder != TELOS_BUILDER_NATIVE &&
         options->builder != TELOS_BUILDER_CONTAINER) ||
        options->goal < TELOS_INSTALL_GOAL_INSTALL ||
        options->goal > TELOS_INSTALL_GOAL_TEST ||
        realpath(options->state_directory, state_directory) == NULL) {
        set_error(
            error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
            "Plugin install options and existing state directory are required");
        return false;
    }
    if (telos_cancel_requested(cancel)) {
        set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                  "Plugin installation was cancelled");
        return false;
    }

    progress(options, TELOS_INSTALL_RESOLVE);
    git_source = strncmp(options->source, "git:", 4) == 0;
    if (!git_source && strncmp(options->source, "local:", 6) != 0 &&
        stat(options->source, &status) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENOENT,
                  "Plugin source is not a local directory or git: source");
        return false;
    }
    if (!path_join(quarantine_root, sizeof(quarantine_root), state_directory,
                   "quarantine") ||
        !make_directories(quarantine_root, 0700, error) ||
        snprintf(quarantine_template, sizeof(quarantine_template),
                 "%s/install-XXXXXX",
                 quarantine_root) >= (int)sizeof(quarantine_template)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Plugin quarantine could not be created");
        }
        goto cleanup;
    }
    quarantine = mkdtemp(quarantine_template);
    if (quarantine == NULL ||
        !path_join(source_directory, sizeof(source_directory), quarantine,
                   "source") ||
        !path_join(build_directory, sizeof(build_directory), quarantine,
                   "build") ||
        !path_join(staging_directory, sizeof(staging_directory), quarantine,
                   "staging") ||
        !path_join(log_path, sizeof(log_path), quarantine, "build.log")) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Plugin quarantine could not be created");
        }
        goto cleanup;
    }

    progress(options, TELOS_INSTALL_FETCH_TO_QUARANTINE);
    if (!fetch_source(options, source_directory, quarantine, log_path,
                      git_source, cancel, error)) {
        goto cleanup;
    }
    if (!path_join(manifest_path, sizeof(manifest_path), source_directory,
                   "plugin.toml") ||
        !path_join(lock_path, sizeof(lock_path), source_directory,
                   "telos.lock")) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, ENAMETOOLONG,
                  "Plugin metadata path is too long");
        goto cleanup;
    }

    progress(options, TELOS_INSTALL_INSPECT);
    manifest = telos_plugin_manifest_load(manifest_path, error);
    if (manifest == NULL) {
        goto cleanup;
    }
    lock = telos_plugin_lock_load(lock_path, error);
    if (lock == NULL) {
        goto cleanup;
    }
    progress(options, TELOS_INSTALL_PLAN);
    risk = inspect_risk(options, manifest, lock, git_source);
    progress(options, TELOS_INSTALL_AUTHORIZE);
    if (risk.requires_approval &&
        (options->approve == NULL ||
         !options->approve(&risk, options->approve_context))) {
        set_error(error, TELOS_ERROR_DOMAIN_PERMISSION, EPERM,
                  "Plugin installation risk was not approved");
        goto cleanup;
    }
    progress(options, TELOS_INSTALL_VERIFY_DEPENDENCIES);
    if (!telos_plugin_lock_verify(lock, source_directory, error) ||
        (!risk.unlocked_source &&
         !telos_plugin_lock_verify_source(lock, source_directory, error))) {
        goto cleanup;
    }

    if (!write_build_inputs(source_directory, options, build_input_path,
                            sizeof(build_input_path), error) ||
        !telos_sha256_directory(source_directory, digest)) {
        if (error == NULL || *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Plugin build inputs could not be hashed");
        }
        goto cleanup;
    }
    unlink(build_input_path);
    build_input_path[0] = '\0';
    if (snprintf(cache_root, sizeof(cache_root), "%s/cache/plugins",
                 state_directory) >= (int)sizeof(cache_root) ||
        !make_directories(cache_root, 0700, error) ||
        !path_join(cache_entry, sizeof(cache_entry), cache_root, digest) ||
        !path_join(cached_staged, sizeof(cached_staged), cache_entry,
                   "staged")) {
        goto cleanup;
    }
    cache_hit = stat(cache_entry, &status) == 0 && S_ISDIR(status.st_mode);
    if (cache_hit) {
        if (!find_artifact_recursive(cached_staged, artifact,
                                     sizeof(artifact))) {
            set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EBADMSG,
                      "Plugin cache entry is incomplete");
            goto cleanup;
        }
    } else {
        if (!build_plugin(options, cancel, source_directory, build_directory,
                          staging_directory, log_path, artifact,
                          sizeof(artifact), error)) {
            goto cleanup;
        }
        if (!create_cache_entry(options, cancel, quarantine, source_directory,
                                staging_directory, log_path, cache_entry,
                                error)) {
            goto cleanup;
        }
        if (!find_artifact_recursive(cached_staged, artifact,
                                     sizeof(artifact))) {
            set_error(error, TELOS_ERROR_DOMAIN_PLUGIN, EBADMSG,
                      "Committed Plugin cache entry is incomplete");
            goto cleanup;
        }
    }

    if (!health_check_plugin(options, cancel, source_directory, log_path,
                             artifact, telos_plugin_manifest_id(manifest),
                             error)) {
        goto cleanup;
    }

    if (options->goal == TELOS_INSTALL_GOAL_INSTALL) {
        progress(options, TELOS_INSTALL_ACTIVATE);
        if (!activate_cache_entry(state_directory,
                                  telos_plugin_manifest_id(manifest),
                                  cache_entry, error)) {
            progress(options, TELOS_INSTALL_ROLLBACK);
            goto cleanup;
        }
    }
    progress(options, TELOS_INSTALL_COMMIT);
    result->plugin_id = copy_text(telos_plugin_manifest_id(manifest));
    result->version = copy_text(telos_plugin_manifest_version(manifest));
    result->cache_key = copy_text(digest);
    result->artifact_path = copy_text(artifact);
    result->cache_hit = cache_hit;
    if (result->plugin_id == NULL || result->version == NULL ||
        result->cache_key == NULL || result->artifact_path == NULL) {
        telos_install_result_clear(result);
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Plugin installation result could not be allocated");
        goto cleanup;
    }
    progress(options, TELOS_INSTALL_COMPLETED);
    success = true;

cleanup:
    if (build_input_path[0] != '\0') {
        unlink(build_input_path);
    }
    telos_plugin_lock_destroy(lock);
    telos_plugin_manifest_destroy(manifest);
    if (quarantine != NULL && success) {
        remove_tree(quarantine);
    }
    return success;
}

static bool cache_key_valid(const char *cache_key)
{
    if (cache_key == NULL || strlen(cache_key) != 64) {
        return false;
    }
    for (size_t index = 0; index < 64; ++index) {
        if (!((cache_key[index] >= '0' && cache_key[index] <= '9') ||
              (cache_key[index] >= 'a' && cache_key[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool telos_plugin_activate(const char *state_directory,
                           const char *plugin_id,
                           const char *cache_key,
                           struct telos_error **error)
{
    char canonical_state[PATH_BUFFER_SIZE];
    char cache_entry[PATH_BUFFER_SIZE];
    char staged[PATH_BUFFER_SIZE];
    char artifact[PATH_BUFFER_SIZE];

    if (error != NULL) {
        *error = NULL;
    }
    if (state_directory == NULL || plugin_id == NULL || plugin_id[0] == '\0' ||
        strchr(plugin_id, '/') != NULL || !cache_key_valid(cache_key) ||
        realpath(state_directory, canonical_state) == NULL ||
        snprintf(cache_entry, sizeof(cache_entry), "%s/cache/plugins/%s",
                 canonical_state, cache_key) >= (int)sizeof(cache_entry) ||
        !path_join(staged, sizeof(staged), cache_entry, "staged") ||
        !find_artifact_recursive(staged, artifact, sizeof(artifact))) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin activation target is invalid or unavailable");
        return false;
    }
    return activate_cache_entry(canonical_state, plugin_id, cache_entry, error);
}

bool telos_plugin_remove(const char *state_directory,
                         const char *plugin_id,
                         struct telos_error **error)
{
    char canonical_state[PATH_BUFFER_SIZE];
    char plugin_directory[PATH_BUFFER_SIZE];
    struct stat status;

    if (error != NULL) {
        *error = NULL;
    }
    if (state_directory == NULL || plugin_id == NULL || plugin_id[0] == '\0' ||
        strchr(plugin_id, '/') != NULL ||
        realpath(state_directory, canonical_state) == NULL ||
        snprintf(plugin_directory, sizeof(plugin_directory), "%s/plugins/%s",
                 canonical_state, plugin_id) >= (int)sizeof(plugin_directory)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin removal arguments are invalid");
        return false;
    }
    if (lstat(plugin_directory, &status) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOENT,
                  "Plugin is not installed");
        return false;
    }
    errno = 0;
    if (!remove_tree(plugin_directory)) {
        int saved_errno = errno;

        set_error(error, TELOS_ERROR_DOMAIN_IO,
                  saved_errno == 0 ? EIO : saved_errno,
                  "Plugin activation could not be removed");
        return false;
    }
    return true;
}

bool telos_plugin_rollback(const char *state_directory,
                           const char *plugin_id,
                           struct telos_error **error)
{
    char canonical_state[PATH_BUFFER_SIZE];
    char plugin_directory[PATH_BUFFER_SIZE];
    char current[PATH_BUFFER_SIZE];
    char previous[PATH_BUFFER_SIZE];
    char current_target[PATH_BUFFER_SIZE];
    char previous_target[PATH_BUFFER_SIZE];
    ssize_t current_size;
    ssize_t previous_size;

    if (error != NULL) {
        *error = NULL;
    }
    if (state_directory == NULL || plugin_id == NULL ||
        strchr(plugin_id, '/') != NULL ||
        realpath(state_directory, canonical_state) == NULL ||
        snprintf(plugin_directory, sizeof(plugin_directory), "%s/plugins/%s",
                 canonical_state, plugin_id) >= (int)sizeof(plugin_directory) ||
        !path_join(current, sizeof(current), plugin_directory, "current") ||
        !path_join(previous, sizeof(previous), plugin_directory, "previous")) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Plugin rollback arguments are invalid");
        return false;
    }
    current_size =
        readlink(current, current_target, sizeof(current_target) - 1);
    previous_size =
        readlink(previous, previous_target, sizeof(previous_target) - 1);
    if (current_size < 0 || previous_size < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ENOENT,
                  "Plugin has no rollback version");
        return false;
    }
    current_target[current_size] = '\0';
    previous_target[previous_size] = '\0';
    return replace_link(current, previous_target, error) &&
           replace_link(previous, current_target, error);
}

void telos_install_result_clear(struct telos_install_result *result)
{
    if (result == NULL) {
        return;
    }
    free(result->artifact_path);
    free(result->cache_key);
    free(result->version);
    free(result->plugin_id);
    memset(result, 0, sizeof(*result));
}
