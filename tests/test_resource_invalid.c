#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <telos/resource.h>

static void write_text(const char *path, const char *text)
{
    FILE *stream = fopen(path, "wb");

    assert(stream != NULL);
    assert(fwrite(text, 1, strlen(text), stream) == strlen(text));
    assert(fclose(stream) == 0);
}

static void clear_error(struct telos_error **error)
{
    assert(*error != NULL);
    telos_error_release(*error);
    *error = NULL;
}

static void assert_reload_rejected(
    struct telos_resource_manager *manager,
    const char *path,
    const char *content,
    struct telos_error **error
)
{
    write_text(path, content);
    assert(!telos_resource_manager_reload(manager, error));
    clear_error(error);
}

int main(void)
{
    char root[] = "/tmp/telos-resource-invalid-XXXXXX";
    char skill[512];
    char skill_path[512];
    char script_directory[512];
    char script_path[512];
    char escaped_path[512];
    char invalid_skill[512];
    char invalid_path[512];
    char ignored_file[512];
    char ignored_directory[512];
    char regular_root[512];
    const char *roots[] = {root};
    const char *capabilities[] = {"process.spawn"};
    const char *unrelated[] = {NULL, "filesystem.read"};
    struct telos_resource_manager *manager;
    struct telos_resource_generation *generation;
    const struct telos_skill *selected;
    struct telos_error *error = NULL;
    char *resolved;

    assert(telos_resource_manager_create(NULL, 1, &error) == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    clear_error(&error);
    {
        const char *null_root[] = {NULL};
        const char *empty_root[] = {""};
        struct telos_resource_manager *empty =
            telos_resource_manager_create(NULL, 0, &error);
        struct telos_resource_generation *empty_generation;

        assert(empty != NULL);
        assert(error == NULL);
        empty_generation = telos_resource_manager_acquire(empty);
        assert(telos_resource_generation_skill_count(empty_generation) == 0);
        telos_resource_generation_release(empty_generation);
        telos_resource_manager_destroy(empty);
        assert(telos_resource_manager_create(null_root, 1, &error) == NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
        clear_error(&error);
        assert(telos_resource_manager_create(empty_root, 1, &error) == NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
        clear_error(&error);
    }
    assert(mkdtemp(root) != NULL);
    assert(snprintf(skill, sizeof(skill), "%s/valid", root)
        < (int)sizeof(skill));
    assert(snprintf(skill_path, sizeof(skill_path), "%s/SKILL.md", skill)
        < (int)sizeof(skill_path));
    assert(snprintf(
        script_directory,
        sizeof(script_directory),
        "%s/scripts",
        skill
    ) < (int)sizeof(script_directory));
    assert(snprintf(
        script_path,
        sizeof(script_path),
        "%s/not-executable.sh",
        script_directory
    ) < (int)sizeof(script_path));
    assert(snprintf(
        escaped_path,
        sizeof(escaped_path),
        "%s/escaped",
        skill
    ) < (int)sizeof(escaped_path));
    assert(snprintf(
        invalid_skill,
        sizeof(invalid_skill),
        "%s/invalid",
        root
    ) < (int)sizeof(invalid_skill));
    assert(snprintf(
        invalid_path,
        sizeof(invalid_path),
        "%s/SKILL.md",
        invalid_skill
    ) < (int)sizeof(invalid_path));
    assert(snprintf(
        ignored_file,
        sizeof(ignored_file),
        "%s/not-a-directory",
        root
    ) < (int)sizeof(ignored_file));
    assert(snprintf(
        ignored_directory,
        sizeof(ignored_directory),
        "%s/no-skill",
        root
    ) < (int)sizeof(ignored_directory));
    assert(snprintf(
        regular_root,
        sizeof(regular_root),
        "%s/regular-root",
        root
    ) < (int)sizeof(regular_root));

    assert(mkdir(skill, 0700) == 0);
    assert(mkdir(script_directory, 0700) == 0);
    write_text(
        skill_path,
        "---\n"
        "name: valid\n"
        "description: A valid fixture.\n"
        "license: Apache-2.0\n"
        "---\n"
        "Instructions.\n"
    );
    write_text(script_path, "#!/bin/sh\nexit 0\n");
    assert(symlink("/etc/passwd", escaped_path) == 0);
    write_text(ignored_file, "ignored\n");
    assert(mkdir(ignored_directory, 0700) == 0);
    write_text(regular_root, "not a directory\n");
    {
        const char *bad_roots[] = {regular_root};

        assert(telos_resource_manager_create(bad_roots, 1, &error) == NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_IO);
        clear_error(&error);
    }

    manager = telos_resource_manager_create(roots, 1, &error);
    assert(manager != NULL);
    assert(error == NULL);
    generation = telos_resource_manager_acquire(manager);
    assert(telos_resource_generation_skill_count(generation) == 1);
    assert(telos_resource_generation_skill_at(generation, 1) == NULL);
    assert(telos_resource_generation_skill_at(NULL, 0) == NULL);
    assert(telos_resource_generation_retain(NULL) == NULL);
    assert(telos_resource_generation_retain(generation) == generation);
    telos_resource_generation_release(generation);
    selected = telos_resource_generation_select_skill(
        generation,
        "$valid",
        &error
    );
    assert(selected != NULL);
    assert(strcmp(telos_skill_description(selected), "A valid fixture.") == 0);
    assert(!telos_skill_has_openai_metadata(selected));
    assert(telos_resource_generation_select_skill(
        generation,
        "$valid with arguments",
        &error
    ) == selected);
    assert(telos_resource_generation_select_skill(
        generation,
        "Please use VALID now",
        &error
    ) == selected);
    assert(telos_resource_generation_select_skill(
        generation,
        "This is A VALID FIXTURE",
        &error
    ) == selected);
    assert(telos_resource_generation_select_skill(
        NULL,
        "$valid",
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_resource_generation_select_skill(
        generation,
        NULL,
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_resource_generation_select_skill(
        generation,
        "",
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_resource_generation_select_skill(
        generation,
        "no match",
        &error
    ) == NULL);
    assert(telos_error_code(error) != 0);
    clear_error(&error);

    assert(telos_skill_resolve_path(NULL, "SKILL.md", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, NULL, &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, ".", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "..", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "scripts//x", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "scripts/./x", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "scripts/../x", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "scripts/", &error) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "/etc/passwd", &error) == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "missing", &error) == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_IO);
    clear_error(&error);
    assert(telos_skill_resolve_path(selected, "escaped", &error) == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION);
    clear_error(&error);
    resolved = telos_skill_resolve_script(
        selected,
        "scripts/not-executable.sh",
        capabilities,
        1,
        &error
    );
    assert(resolved == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_PERMISSION);
    clear_error(&error);
    assert(telos_skill_resolve_script(
        selected,
        "scripts/not-executable.sh",
        NULL,
        1,
        &error
    ) == NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    clear_error(&error);
    assert(telos_skill_resolve_script(
        selected,
        NULL,
        capabilities,
        1,
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_script(
        selected,
        "SKILL.md",
        capabilities,
        1,
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_script(
        selected,
        "scripts/not-executable.sh",
        unrelated,
        2,
        &error
    ) == NULL);
    clear_error(&error);
    assert(telos_skill_resolve_script(
        selected,
        "scripts/",
        capabilities,
        1,
        &error
    ) == NULL);
    clear_error(&error);

    assert(mkdir(invalid_skill, 0700) == 0);
    assert_reload_rejected(
        manager,
        invalid_path,
        "No frontmatter.\n",
        &error
    );
    assert_reload_rejected(
        manager,
        invalid_path,
        "---\nname: invalid\nNo terminator.\n",
        &error
    );
    assert_reload_rejected(
        manager,
        invalid_path,
        "---\nname:\ndescription: fixture\n---\nBody.\n",
        &error
    );
    assert_reload_rejected(
        manager,
        invalid_path,
        "---\nname: invalid\ndescription:\n---\nBody.\n",
        &error
    );
    assert_reload_rejected(
        manager,
        invalid_path,
        "---\nname: invalid\n---\nMissing description.\n",
        &error
    );
    {
        FILE *stream = fopen(invalid_path, "wb");

        assert(stream != NULL);
        assert(fseek(stream, 1024L * 1024L, SEEK_SET) == 0);
        assert(fputc('x', stream) == 'x');
        assert(fclose(stream) == 0);
        assert(!telos_resource_manager_reload(manager, &error));
        assert(telos_error_code(error) == EFBIG);
        clear_error(&error);
    }
    assert(telos_resource_generation_number(generation) == 1);
    {
        struct telos_resource_generation *still_current =
            telos_resource_manager_acquire(manager);

        assert(telos_resource_generation_number(still_current) == 1);
        telos_resource_generation_release(still_current);
    }

    telos_resource_generation_release(generation);
    telos_resource_manager_destroy(manager);
    unlink(invalid_path);
    rmdir(invalid_skill);
    unlink(regular_root);
    rmdir(ignored_directory);
    unlink(ignored_file);
    unlink(escaped_path);
    unlink(script_path);
    unlink(skill_path);
    rmdir(script_directory);
    rmdir(skill);
    rmdir(root);

    assert(telos_resource_manager_reload(NULL, &error) == false);
    clear_error(&error);
    assert(telos_resource_manager_acquire(NULL) == NULL);
    assert(telos_resource_generation_number(NULL) == 0);
    assert(telos_resource_generation_skill_count(NULL) == 0);
    assert(telos_skill_name(NULL) == NULL);
    assert(telos_skill_description(NULL) == NULL);
    assert(telos_skill_instructions(NULL) == NULL);
    assert(!telos_skill_has_openai_metadata(NULL));
    telos_resource_generation_release(NULL);
    telos_resource_manager_destroy(NULL);
    return 0;
}
