#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/resource.h>

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    const size_t size = strlen(text);
    bool result;

    if (file == NULL) {
        return false;
    }
    result = fwrite(text, 1, size, file) == size && fclose(file) == 0;
    return result;
}

static bool join_path(
    char *output,
    size_t output_size,
    const char *directory,
    const char *name
)
{
    const size_t directory_size = strlen(directory);
    const size_t name_size = strlen(name);

    if (directory_size + 1 + name_size + 1 > output_size) {
        return false;
    }
    memcpy(output, directory, directory_size);
    output[directory_size] = '/';
    memcpy(output + directory_size + 1, name, name_size + 1);
    return true;
}

int main(void)
{
    char root[] = "/tmp/telos-skills-XXXXXX";
    char skill_directory[512];
    char agents_directory[512];
    char skill_path[512];
    char metadata_path[512];
    char reference_directory[512];
    char reference_path[512];
    char script_directory[512];
    char script_path[512];
    const char *roots[] = {root};
    const char *script_capabilities[] = {"process.spawn"};
    struct telos_resource_manager *manager;
    struct telos_resource_generation *first;
    struct telos_resource_generation *second;
    const struct telos_skill *selected;
    char *resolved;
    bool passed;

    if (mkdtemp(root) == NULL) {
        return 1;
    }
    if (
        !join_path(skill_directory, sizeof(skill_directory), root, "deploy")
        || !join_path(
            agents_directory,
            sizeof(agents_directory),
            skill_directory,
            "agents"
        )
        || !join_path(
            skill_path,
            sizeof(skill_path),
            skill_directory,
            "SKILL.md"
        )
        || !join_path(
            metadata_path,
            sizeof(metadata_path),
            agents_directory,
            "openai.yaml"
        )
        || !join_path(
            reference_directory,
            sizeof(reference_directory),
            skill_directory,
            "references"
        )
        || !join_path(
            reference_path,
            sizeof(reference_path),
            reference_directory,
            "guide.md"
        )
        || !join_path(
            script_directory,
            sizeof(script_directory),
            skill_directory,
            "scripts"
        )
        || !join_path(
            script_path,
            sizeof(script_path),
            script_directory,
            "deploy.sh"
        )
        || mkdir(skill_directory, 0700) != 0
        || mkdir(agents_directory, 0700) != 0
        || mkdir(reference_directory, 0700) != 0
        || mkdir(script_directory, 0700) != 0
        || !write_text(
            skill_path,
            "---\n"
            "name: deploy\n"
            "description: Deploy a service safely.\n"
            "---\n\n"
            "Run the deployment checks.\n"
        )
        || !write_text(
            metadata_path,
            "policy:\n  allow_implicit_invocation: true\n"
        )
        || !write_text(reference_path, "# Deployment guide\n")
        || !write_text(script_path, "#!/bin/sh\nexit 0\n")
        || chmod(script_path, 0700) != 0
    ) {
        return 1;
    }

    manager = telos_resource_manager_create(roots, 1, NULL);
    first = telos_resource_manager_acquire(manager);
    selected = telos_resource_generation_select_skill(
        first,
        "$deploy",
        NULL
    );
    resolved = telos_skill_resolve_path(
        selected,
        "references/guide.md",
        NULL
    );
    passed = manager != NULL
        && first != NULL
        && telos_resource_generation_number(first) == 1
        && telos_resource_generation_skill_count(first) == 1
        && selected != NULL
        && strcmp(telos_skill_name(selected), "deploy") == 0
        && strstr(
            telos_skill_instructions(selected),
            "Run the deployment checks."
        ) != NULL
        && telos_skill_has_openai_metadata(selected)
        && resolved != NULL;
    telos_resource_string_free(resolved);
    resolved = telos_skill_resolve_script(
        selected,
        "scripts/deploy.sh",
        NULL,
        0,
        NULL
    );
    passed = passed && resolved == NULL;
    resolved = telos_skill_resolve_script(
        selected,
        "scripts/deploy.sh",
        script_capabilities,
        1,
        NULL
    );
    passed = passed && resolved != NULL;
    telos_resource_string_free(resolved);

    passed = passed && write_text(
        skill_path,
        "---\n"
        "name: deploy\n"
        "description: Release production deployments.\n"
        "---\n\n"
        "Use the new release workflow.\n"
    ) && telos_resource_manager_reload(manager, NULL);
    second = telos_resource_manager_acquire(manager);
    selected = telos_resource_generation_select_skill(
        second,
        "please handle a production deployment",
        NULL
    );
    passed = passed
        && second != NULL
        && telos_resource_generation_number(second) == 2
        && selected != NULL
        && strstr(
            telos_skill_instructions(selected),
            "new release workflow"
        ) != NULL
        && strstr(
            telos_skill_instructions(
                telos_resource_generation_select_skill(
                    first,
                    "$deploy",
                    NULL
                )
            ),
            "deployment checks"
        ) != NULL
        && telos_skill_resolve_path(selected, "../outside", NULL) == NULL;

    telos_resource_generation_release(second);
    telos_resource_generation_release(first);
    telos_resource_manager_destroy(manager);
    unlink(reference_path);
    unlink(script_path);
    unlink(metadata_path);
    unlink(skill_path);
    rmdir(reference_directory);
    rmdir(script_directory);
    rmdir(agents_directory);
    rmdir(skill_directory);
    rmdir(root);
    if (!passed) {
        fputs("OpenAI-compatible Skill discovery or Generation failed\n", stderr);
        return 1;
    }
    return 0;
}
