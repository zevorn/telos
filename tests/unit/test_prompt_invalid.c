#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/prompt.h>
#include <telos/plugins/project_guidance.h>

static bool join_path(char *output,
                      size_t output_size,
                      const char *directory,
                      const char *name)
{
    int written = snprintf(output, output_size, "%s/%s", directory, name);

    return written >= 0 && (size_t)written < output_size;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    bool result = file != NULL && fwrite(text, 1, size, file) == size;

    if (file != NULL) {
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool write_large(const char *path)
{
    FILE *file = fopen(path, "wb");
    bool result = file != NULL;

    for (size_t index = 0; result && index < 65537; ++index) {
        result = fputc('x', file) != EOF;
    }
    if (file != NULL) {
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool rejects_fragment(struct telos_prompt_fragment fragment)
{
    struct telos_error *error = NULL;
    struct telos_prompt_snapshot *snapshot =
        telos_prompt_snapshot_create(&fragment, 1, &error);
    bool rejected = snapshot == NULL && error != NULL;

    telos_prompt_snapshot_release(snapshot);
    telos_error_release(error);
    return rejected;
}

static bool test_fragments(void)
{
    const struct telos_prompt_fragment valid[] = {
        {
            .slot = TELOS_PROMPT_EXTERNAL_DATA,
            .trust = TELOS_PROMPT_TRUST_EXTERNAL,
            .priority = 0,
            .byte_budget = 16,
            .source = "external",
            .content = "external",
        },
        {
            .slot = TELOS_PROMPT_PLUGIN_INSTRUCTIONS,
            .trust = TELOS_PROMPT_TRUST_PLUGIN,
            .priority = 0,
            .byte_budget = 16,
            .source = "plugin",
            .content = "plugin",
        },
        {
            .slot = TELOS_PROMPT_SELECTED_SKILL,
            .trust = TELOS_PROMPT_TRUST_USER,
            .priority = 0,
            .byte_budget = 16,
            .source = "skill",
            .content = "skill",
        },
        {
            .slot = TELOS_PROMPT_WORKFLOW,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .priority = 0,
            .byte_budget = 16,
            .source = "workflow",
            .content = "workflow",
        },
        {
            .slot = TELOS_PROMPT_AGENT_DEFINITION,
            .trust = TELOS_PROMPT_TRUST_USER,
            .priority = 0,
            .byte_budget = 16,
            .source = "agent-user",
            .content = "agent-user",
        },
        {
            .slot = TELOS_PROMPT_AGENT_DEFINITION,
            .trust = TELOS_PROMPT_TRUST_PROJECT,
            .priority = 10,
            .byte_budget = 16,
            .source = "agent-project",
            .content = "agent-project",
        },
        {
            .slot = TELOS_PROMPT_PROJECT_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_CORE,
            .priority = 0,
            .byte_budget = 16,
            .source = "project",
            .content = "project",
        },
        {
            .slot = TELOS_PROMPT_USER_GUIDANCE,
            .trust = TELOS_PROMPT_TRUST_CORE,
            .priority = 0,
            .byte_budget = 16,
            .source = "user",
            .content = "user",
        },
        {
            .slot = TELOS_PROMPT_POLICY,
            .trust = TELOS_PROMPT_TRUST_CORE,
            .priority = 0,
            .byte_budget = 16,
            .source = "z-policy",
            .content = "policy-z",
        },
        {
            .slot = TELOS_PROMPT_POLICY,
            .trust = TELOS_PROMPT_TRUST_POLICY,
            .priority = 10,
            .byte_budget = 16,
            .source = "a-policy",
            .content = "policy-a",
        },
    };
    struct telos_prompt_fragment invalid = valid[0];
    struct telos_error *error = NULL;
    struct telos_prompt_snapshot *empty =
        telos_prompt_snapshot_create(NULL, 0, NULL);
    struct telos_prompt_snapshot *snapshot = telos_prompt_snapshot_create(
        valid, sizeof(valid) / sizeof(valid[0]), NULL);
    struct telos_prompt_snapshot *retained =
        telos_prompt_snapshot_retain(snapshot);
    const char *content = telos_prompt_snapshot_content(snapshot);
    bool passed =
        empty != NULL && snapshot != NULL && retained == snapshot &&
        content != NULL &&
        strstr(content, "policy-a") < strstr(content, "policy-z") &&
        strstr(content, "policy-z") < strstr(content, "# USER GUIDANCE") &&
        strstr(content, "# SELECTED SKILL") != NULL &&
        strstr(content, "# PLUGIN INSTRUCTIONS") != NULL &&
        strstr(content, "# EXTERNAL DATA") != NULL &&
        telos_prompt_snapshot_content(NULL) == NULL &&
        telos_prompt_snapshot_retain(NULL) == NULL &&
        strstr(telos_prompt_kernel_contract(),
               "Do not invent unavailable tools") != NULL &&
        strstr(telos_prompt_kernel_contract(),
               "communicate concise progress") != NULL &&
        strstr(telos_prompt_kernel_contract(),
               "distinguish completed, unverified, and blocked work") != NULL &&
        strchr(telos_prompt_kernel_contract(), '\n') == NULL &&
        telos_prompt_snapshot_create(NULL, 1, &error) == NULL && error != NULL;

    telos_error_release(error);
    error = NULL;
    invalid.source = NULL;
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.source = "";
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.content = NULL;
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.byte_budget = 0;
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.byte_budget = 3;
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.slot = 0;
    passed = passed && rejects_fragment(invalid);
    invalid = valid[0];
    invalid.slot = TELOS_PROMPT_POLICY;
    invalid.trust = TELOS_PROMPT_TRUST_USER;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_USER_GUIDANCE;
    invalid.trust = TELOS_PROMPT_TRUST_PROJECT;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_PROJECT_GUIDANCE;
    invalid.trust = TELOS_PROMPT_TRUST_USER;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_AGENT_DEFINITION;
    invalid.trust = TELOS_PROMPT_TRUST_PLUGIN;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_WORKFLOW;
    invalid.trust = TELOS_PROMPT_TRUST_EXTERNAL;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_SELECTED_SKILL;
    passed = passed && rejects_fragment(invalid);
    invalid.slot = TELOS_PROMPT_PLUGIN_INSTRUCTIONS;
    invalid.trust = TELOS_PROMPT_TRUST_PROJECT;
    passed = passed && rejects_fragment(invalid);

    telos_prompt_snapshot_release(retained);
    telos_prompt_snapshot_release(snapshot);
    telos_prompt_snapshot_release(empty);
    telos_prompt_snapshot_release(NULL);
    telos_prompt_string_free(NULL);
    return passed;
}

static bool
discover_rejected(const char *home, const char *root, const char *current)
{
    char *user = (char *)1;
    char *project = (char *)1;
    struct telos_error *error = NULL;
    bool rejected = !telos_guidance_discover(home, root, current, &user,
                                             &project, &error) &&
                    user == NULL && project == NULL && error != NULL;

    telos_error_release(error);
    return rejected;
}

static bool test_guidance(void)
{
    char workspace[] = "/tmp/telos-prompt-invalid-XXXXXX";
    char home[512];
    char root[512];
    char nested[512];
    char outside[512];
    char user_file[512];
    char root_file[512];
    char *user = NULL;
    char *project = NULL;
    struct telos_error *error = NULL;
    bool passed;

    if (mkdtemp(workspace) == NULL ||
        !join_path(home, sizeof(home), workspace, "home") ||
        !join_path(root, sizeof(root), workspace, "root") ||
        !join_path(nested, sizeof(nested), root, "nested") ||
        !join_path(outside, sizeof(outside), workspace, "outside") ||
        !join_path(user_file, sizeof(user_file), home, "AGENTS.md") ||
        !join_path(root_file, sizeof(root_file), root, "AGENTS.md") ||
        mkdir(home, 0700) != 0 || mkdir(root, 0700) != 0 ||
        mkdir(nested, 0700) != 0 || mkdir(outside, 0700) != 0) {
        return false;
    }

    passed =
        telos_guidance_discover(home, root, nested, &user, &project, NULL) &&
        user != NULL && project != NULL && user[0] == '\0' &&
        project[0] == '\0';
    telos_prompt_string_free(user);
    telos_prompt_string_free(project);
    user = NULL;
    project = NULL;

    passed =
        passed &&
        !telos_guidance_discover(NULL, root, nested, &user, &project, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        !telos_guidance_discover(home, NULL, nested, &user, &project, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        !telos_guidance_discover(home, root, NULL, &user, &project, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        !telos_guidance_discover(home, root, nested, NULL, &project, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed =
        passed &&
        !telos_guidance_discover(home, root, nested, &user, NULL, &error) &&
        error != NULL;
    telos_error_release(error);
    error = NULL;
    passed = passed && discover_rejected(home, "/missing/telos-root", nested) &&
             discover_rejected(home, root, outside) && write_large(user_file) &&
             discover_rejected(home, root, nested);

    unlink(user_file);
    passed = passed && mkdir(root_file, 0700) == 0 &&
             discover_rejected(home, root, nested);
    rmdir(root_file);
    passed = passed && write_text(user_file, "") &&
             write_text(root_file, "root") &&
             telos_guidance_discover(home, root, root, &user, &project, NULL) &&
             user != NULL && user[0] == '\0' && strcmp(project, "root") == 0;

    telos_prompt_string_free(user);
    telos_prompt_string_free(project);
    unlink(root_file);
    unlink(user_file);
    rmdir(outside);
    rmdir(nested);
    rmdir(root);
    rmdir(home);
    rmdir(workspace);
    return passed;
}

int main(void)
{
    bool fragments_passed = test_fragments();
    bool guidance_passed = test_guidance();

    if (!fragments_passed) {
        fputs("Prompt validation failure matrix failed\n", stderr);
    }
    if (!guidance_passed) {
        fputs("Guidance failure matrix failed\n", stderr);
    }
    if (!fragments_passed || !guidance_passed) {
        return 1;
    }
    return 0;
}
