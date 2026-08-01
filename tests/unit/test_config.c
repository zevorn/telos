#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/config.h>

static void write_file(const char *path, const char *content)
{
    FILE *stream = fopen(path, "wb");

    assert(stream != NULL);
    assert(fwrite(content, 1, strlen(content), stream) == strlen(content));
    assert(fclose(stream) == 0);
}

int main(void)
{
    char root[] = "/tmp/telos-config-XXXXXX";
    char home[4096];
    char telos_home[4096];
    char user_config[4096];
    char project[4096];
    char project_config[4096];
    struct telos_config *config;
    struct telos_error *error = NULL;

    assert(mkdtemp(root) != NULL);
    assert(snprintf(home, sizeof(home), "%s/home", root) < (int)sizeof(home));
    assert(snprintf(telos_home, sizeof(telos_home), "%s/home/.telos", root) <
           (int)sizeof(telos_home));
    assert(snprintf(user_config, sizeof(user_config),
                    "%s/home/.telos/config.toml",
                    root) < (int)sizeof(user_config));
    assert(snprintf(project, sizeof(project), "%s/project", root) <
           (int)sizeof(project));
    assert(snprintf(project_config, sizeof(project_config),
                    "%s/project/telos.toml",
                    root) < (int)sizeof(project_config));
    assert(mkdir(home, 0700) == 0);
    assert(mkdir(telos_home, 0700) == 0);
    assert(mkdir(project, 0700) == 0);
    write_file(
        user_config,
        "[agent]\nprovider = \"user-provider\"\nmodel = \"user-model\"\n");
    write_file(project_config, "[agent]\nprovider = \"project-provider\"\n"
                               "[builder]\nbackend = \"native\"\n");
    assert(setenv("TELOS_AGENT_PROVIDER", "environment-provider", 1) == 0);
    assert(setenv("TELOS_AGENT_ENDPOINT", "https://environment.invalid/v1",
                  1) == 0);

    config = telos_config_load(home, project, &error);
    assert(config != NULL);
    assert(error == NULL);
    assert(strcmp(telos_config_get(config, "agent.provider"),
                  "environment-provider") == 0);
    assert(telos_config_get_origin(config, "agent.provider") ==
           TELOS_CONFIG_ENVIRONMENT);
    assert(strcmp(telos_config_get(config, "agent.model"), "user-model") == 0);
    assert(telos_config_get_origin(config, "agent.model") == TELOS_CONFIG_USER);
    assert(strcmp(telos_config_get(config, "agent.endpoint"),
                  "https://environment.invalid/v1") == 0);
    assert(telos_config_get_origin(config, "agent.endpoint") ==
           TELOS_CONFIG_ENVIRONMENT);
    assert(strcmp(telos_config_get(config, "builder.backend"), "native") == 0);
    assert(telos_config_override(config, "agent.provider", "cli-provider",
                                 &error));
    assert(strcmp(telos_config_get(config, "agent.provider"), "cli-provider") ==
           0);
    assert(telos_config_get_origin(config, "agent.provider") ==
           TELOS_CONFIG_COMMAND_LINE);
    assert(!telos_config_override(config, "unknown.key", "x", &error));
    assert(error != NULL);
    telos_error_release(error);
    telos_config_destroy(config);

    unsetenv("TELOS_AGENT_PROVIDER");
    unsetenv("TELOS_AGENT_ENDPOINT");
    unlink(project_config);
    unlink(user_config);
    rmdir(project);
    rmdir(telos_home);
    rmdir(home);
    rmdir(root);
    return 0;
}
