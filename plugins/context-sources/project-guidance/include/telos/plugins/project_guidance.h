#ifndef TELOS_PLUGINS_PROJECT_GUIDANCE_H
#define TELOS_PLUGINS_PROJECT_GUIDANCE_H

#include <telos/error.h>
#include <telos/types.h>

typedef bool (*telos_guidance_discover_fn)(const char *telos_home,
                                           const char *project_root,
                                           const char *current_directory,
                                           char **user_guidance,
                                           char **project_guidance,
                                           struct telos_error **error);

struct telos_project_guidance_definition_v1 {
    uint32_t struct_size;
    const char *id;
    telos_guidance_discover_fn discover;
    void (*free_string)(char *value);
};

bool telos_guidance_discover(const char *telos_home,
                             const char *project_root,
                             const char *current_directory,
                             char **user_guidance,
                             char **project_guidance,
                             struct telos_error **error);

void telos_prompt_string_free(char *value);

#endif
