#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/config.h>

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

static bool load_rejected(const char *home,
                          const char *project,
                          const char *path,
                          const char *content)
{
    struct telos_error *error = NULL;
    struct telos_config *config;
    bool rejected = write_text(path, content);

    config = telos_config_load(home, project, &error);
    rejected = rejected && config == NULL && error != NULL;
    telos_config_destroy(config);
    telos_error_release(error);
    return rejected;
}

static bool override_rejected(struct telos_config *config,
                              const char *key,
                              const char *value)
{
    struct telos_error *error = NULL;
    bool rejected =
        !telos_config_override(config, key, value, &error) && error != NULL;

    telos_error_release(error);
    return rejected;
}

int main(void)
{
    char root[] = "/tmp/telos-config-invalid-XXXXXX";
    char home[512];
    char telos_home[512];
    char project[512];
    char config_path[512];
    char long_path[5000];
    struct telos_error *error = NULL;
    struct telos_config *config;
    bool passed;

    if (mkdtemp(root) == NULL ||
        snprintf(home, sizeof(home), "%s/home", root) < 0 ||
        snprintf(telos_home, sizeof(telos_home), "%s/.telos", home) < 0 ||
        snprintf(project, sizeof(project), "%s/project", root) < 0 ||
        snprintf(config_path, sizeof(config_path), "%s/telos.toml", project) <
            0 ||
        mkdir(home, 0700) != 0 || mkdir(telos_home, 0700) != 0 ||
        mkdir(project, 0700) != 0) {
        return 1;
    }

    passed = telos_config_load(NULL, project, &error) == NULL && error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && telos_config_load("relative", project, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && telos_config_load(home, NULL, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && telos_config_load(home, "relative", &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    long_path[0] = '/';
    memset(long_path + 1, 'x', sizeof(long_path) - 2);
    long_path[sizeof(long_path) - 1] = '\0';
    passed = passed && telos_config_load(long_path, project, &error) == NULL &&
             error != NULL;
    telos_error_release(error);
    error = NULL;

    passed =
        passed &&
        load_rejected(home, project, config_path, "key = \"value\"\n") &&
        load_rejected(home, project, config_path, "[agent\n") &&
        load_rejected(home, project, config_path, "[agent]\ninvalid\n") &&
        load_rejected(home, project, config_path,
                      "[agent]\nunknown = \"value\"\n") &&
        load_rejected(home, project, config_path, "[agent]\nmodel = value\n") &&
        load_rejected(home, project, config_path,
                      "[builder]\nbackend = \"unknown\"\n") &&
        load_rejected(home, project, config_path,
                      "[providers.openai]\nstate_mode = \"remote\"\n");

    passed =
        passed && write_text(config_path, "# comment\n"
                                          "[agent]\n"
                                          "provider = \"provider\"\n"
                                          "model = \"model\"\n"
                                          "[state]\n"
                                          "directory = \"/tmp/telos-state\"\n"
                                          "[builder]\n"
                                          "backend = \"container\"\n");
    config = telos_config_load(home, project, NULL);
    passed = passed && config != NULL &&
             telos_config_get(NULL, "agent.model") == NULL &&
             telos_config_get(config, NULL) == NULL &&
             telos_config_get(config, "missing") == NULL &&
             telos_config_get_origin(NULL, "agent.model") == 0 &&
             telos_config_get_origin(config, NULL) == 0 &&
             telos_config_get_origin(config, "missing") == 0 &&
             override_rejected(NULL, "agent.model", "model") &&
             override_rejected(config, NULL, "model") &&
             override_rejected(config, "agent.model", NULL) &&
             override_rejected(config, "agent.model", "") &&
             override_rejected(config, "builder.backend", "other") &&
             override_rejected(config, "providers.openai.secret", "secret:x") &&
             override_rejected(config, "state.directory", "relative") &&
             telos_config_override(config, "builder.backend", "native", NULL) &&
             telos_config_override(config, "state.directory", "/tmp/new", NULL);
    telos_config_destroy(config);
    telos_config_destroy(NULL);

    unlink(config_path);
    rmdir(project);
    rmdir(telos_home);
    rmdir(home);
    rmdir(root);
    if (!passed) {
        fputs("Configuration validation matrix failed\n", stderr);
        return 1;
    }
    return 0;
}
