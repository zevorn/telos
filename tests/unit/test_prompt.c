#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/prompt.h>
#include <telos/plugins/project_guidance.h>

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    const size_t size = strlen(text);

    return file != NULL && fwrite(text, 1, size, file) == size &&
           fclose(file) == 0;
}

static bool join_path(char *output,
                      size_t output_size,
                      const char *directory,
                      const char *name)
{
    size_t directory_size = strlen(directory);
    size_t name_size = strlen(name);

    if (directory_size + name_size + 2 > output_size) {
        return false;
    }
    memcpy(output, directory, directory_size);
    output[directory_size] = '/';
    memcpy(output + directory_size + 1, name, name_size + 1);
    return true;
}

int main(void)
{
    char workspace[] = "/tmp/telos-prompt-XXXXXX";
    char home[512];
    char project[512];
    char nested[512];
    char user_file[512];
    char root_file[512];
    char nested_file[512];
    char *user = NULL;
    char *project_guidance = NULL;
    struct telos_prompt_snapshot *first;
    struct telos_prompt_snapshot *second;
    bool passed;

    if (mkdtemp(workspace) == NULL ||
        !join_path(home, sizeof(home), workspace, "home") ||
        !join_path(project, sizeof(project), workspace, "project") ||
        !join_path(nested, sizeof(nested), project, "nested") ||
        !join_path(user_file, sizeof(user_file), home, "AGENTS.md") ||
        !join_path(root_file, sizeof(root_file), project, "AGENTS.md") ||
        !join_path(nested_file, sizeof(nested_file), nested, "AGENTS.md") ||
        mkdir(home, 0700) != 0 || mkdir(project, 0700) != 0 ||
        mkdir(nested, 0700) != 0 || !write_text(user_file, "user guidance") ||
        !write_text(root_file, "root guidance") ||
        !write_text(nested_file, "nested guidance") ||
        !telos_guidance_discover(home, project, nested, &user,
                                 &project_guidance, NULL)) {
        return 1;
    }

    {
        const struct telos_prompt_fragment fragments[] = {
            {
                .slot = TELOS_PROMPT_EXTERNAL_DATA,
                .trust = TELOS_PROMPT_TRUST_EXTERNAL,
                .priority = 0,
                .byte_budget = 64,
                .source = "retrieval",
                .content = "external data",
            },
            {
                .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
                .trust = TELOS_PROMPT_TRUST_PROJECT,
                .priority = 0,
                .byte_budget = 128,
                .source = "AGENTS.md",
                .content = project_guidance,
            },
            {
                .slot = TELOS_PROMPT_USER_GUIDANCE,
                .trust = TELOS_PROMPT_TRUST_USER,
                .priority = 0,
                .byte_budget = 64,
                .source = "~/.telos/AGENTS.md",
                .content = user,
            },
            {
                .slot = TELOS_PROMPT_POLICY,
                .trust = TELOS_PROMPT_TRUST_POLICY,
                .priority = 0,
                .byte_budget = 64,
                .source = "policy",
                .content = "enforced policy",
            },
        };

        first = telos_prompt_snapshot_create(
            fragments, sizeof(fragments) / sizeof(fragments[0]), NULL);
    }
    passed =
        first != NULL &&
        strstr(telos_prompt_snapshot_content(first), "enforced policy") <
            strstr(telos_prompt_snapshot_content(first), "user guidance") &&
        strstr(telos_prompt_snapshot_content(first), "user guidance") <
            strstr(telos_prompt_snapshot_content(first), "root guidance") &&
        strstr(telos_prompt_snapshot_content(first), "root guidance") <
            strstr(telos_prompt_snapshot_content(first), "nested guidance") &&
        strstr(telos_prompt_snapshot_content(first), "nested guidance") <
            strstr(telos_prompt_snapshot_content(first), "external data");

    telos_prompt_string_free(project_guidance);
    project_guidance = NULL;
    telos_prompt_string_free(user);
    user = NULL;
    passed = passed && write_text(nested_file, "updated nested guidance") &&
             telos_guidance_discover(home, project, nested, &user,
                                     &project_guidance, NULL);
    {
        const struct telos_prompt_fragment fragment = {
            .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .byte_budget = 128,
            .source = "AGENTS.md",
            .content = project_guidance,
        };

        second = telos_prompt_snapshot_create(&fragment, 1, NULL);
    }
    passed = passed && second != NULL &&
             strstr(telos_prompt_snapshot_content(first), "nested guidance") !=
                 NULL &&
             strstr(telos_prompt_snapshot_content(first),
                    "updated nested guidance") == NULL &&
             strstr(telos_prompt_snapshot_content(second),
                    "updated nested guidance") != NULL;

    telos_prompt_snapshot_release(second);
    telos_prompt_snapshot_release(first);
    telos_prompt_string_free(project_guidance);
    telos_prompt_string_free(user);
    unlink(nested_file);
    unlink(root_file);
    unlink(user_file);
    rmdir(nested);
    rmdir(project);
    rmdir(home);
    rmdir(workspace);
    if (!passed) {
        fputs("Prompt ordering, guidance precedence, or snapshot failed\n",
              stderr);
        return 1;
    }
    return 0;
}
