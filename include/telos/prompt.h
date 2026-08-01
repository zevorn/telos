#ifndef TELOS_PROMPT_H
#define TELOS_PROMPT_H

#include <telos/types.h>

#include <telos/error.h>

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
typedef struct telos_prompt_snapshot telos_prompt_snapshot;

telos_prompt_snapshot *
telos_prompt_snapshot_create(const struct telos_prompt_fragment *fragments,
                             size_t fragment_count,
                             struct telos_error **error);

telos_prompt_snapshot *
telos_prompt_snapshot_retain(const telos_prompt_snapshot *snapshot);

void telos_prompt_snapshot_release(const telos_prompt_snapshot *snapshot);

const char *
telos_prompt_snapshot_content(const telos_prompt_snapshot *snapshot);

const char *telos_prompt_kernel_contract(void);

#endif
