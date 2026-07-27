#ifndef TELOS_PROMPT_H
#define TELOS_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

#include <telos/error.h>

#ifdef __cplusplus
extern "C" {
#endif

enum telos_prompt_slot {
    TELOS_PROMPT_POLICY = 1,
    TELOS_PROMPT_USER_GUIDANCE,
    TELOS_PROMPT_PROJECT_GUIDANCE,
    TELOS_PROMPT_AGENT_DEFINITION,
    TELOS_PROMPT_WORKFLOW,
    TELOS_PROMPT_SELECTED_SKILL,
    TELOS_PROMPT_PLUGIN_INSTRUCTIONS,
    TELOS_PROMPT_EXTERNAL_DATA,
};

enum telos_prompt_trust {
    TELOS_PROMPT_TRUST_CORE = 1,
    TELOS_PROMPT_TRUST_POLICY,
    TELOS_PROMPT_TRUST_USER,
    TELOS_PROMPT_TRUST_PROJECT,
    TELOS_PROMPT_TRUST_PLUGIN,
    TELOS_PROMPT_TRUST_EXTERNAL,
};

struct telos_prompt_fragment {
    enum telos_prompt_slot slot;
    enum telos_prompt_trust trust;
    int priority;
    size_t byte_budget;
    const char *source;
    const char *content;
};

struct telos_prompt_snapshot;

struct telos_prompt_snapshot *telos_prompt_snapshot_create(
    const struct telos_prompt_fragment *fragments,
    size_t fragment_count,
    struct telos_error **error
);

struct telos_prompt_snapshot *telos_prompt_snapshot_retain(
    const struct telos_prompt_snapshot *snapshot
);

void telos_prompt_snapshot_release(
    const struct telos_prompt_snapshot *snapshot
);

const char *telos_prompt_snapshot_content(
    const struct telos_prompt_snapshot *snapshot
);

const char *telos_prompt_kernel_contract(void);

bool telos_guidance_discover(
    const char *telos_home,
    const char *project_root,
    const char *current_directory,
    char **user_guidance,
    char **project_guidance,
    struct telos_error **error
);

void telos_prompt_string_free(char *value);

#ifdef __cplusplus
}
#endif

#endif
