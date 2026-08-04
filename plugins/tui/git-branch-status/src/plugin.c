#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/tui_plugin.h>

/*
 * Git Branch Status — example TUI plugin
 *
 * Demonstrates the TUI plugin extension point by:
 *   1. Showing the current git branch in the terminal footer.
 *   2. Registering a keybinding (Alt+B) to force-refresh the branch name.
 *   3. Listening for turn-completed events to refresh the branch.
 *
 * The plugin shells out to `git branch --show-current` in the working
 * directory.  When git is not available or the directory is not a
 * repository the footer section hides itself.
 */

#define BRANCH_SIZE 128U

static char cached_branch[BRANCH_SIZE];
static const char *working_directory;
static bool visible = true;

/* ------------------------------------------------------------------ */
/* Footer section                                                       */
/* ------------------------------------------------------------------ */

static size_t footer_render(void *context, char *buffer, size_t buffer_size)
{
    (void)context;

    if (!visible || cached_branch[0] == '\0') {
        return 0;
    }
    return (size_t)snprintf(buffer, buffer_size, "git:%s", cached_branch);
}

/* ------------------------------------------------------------------ */
/* Keybinding handler                                                   */
/* ------------------------------------------------------------------ */

static bool refresh_branch(void);

static bool keybinding_handler(void *context, struct telos_tui_host *host,
                               struct telos_error **error)
{
    (void)context;
    (void)host;
    (void)error;

    visible = !visible;
    if (visible) {
        refresh_branch();
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Event hook                                                           */
/* ------------------------------------------------------------------ */

static void on_turn_event(void *context,
                          const struct telos_frontend_event *event)
{
    (void)context;

    if (event->kind == TELOS_FRONTEND_TOOL_COMPLETED ||
        event->kind == TELOS_FRONTEND_TOOL_FAILED) {
        refresh_branch();
    }
}

/* ------------------------------------------------------------------ */
/* Git helpers                                                          */
/* ------------------------------------------------------------------ */

static bool refresh_branch(void)
{
    char command[512];
    FILE *pipe;
    size_t size;

    if (working_directory == NULL || working_directory[0] == '\0') {
        cached_branch[0] = '\0';
        return false;
    }
    if (snprintf(command, sizeof(command),
                 "cd '%s' && git branch --show-current 2>/dev/null",
                 working_directory) >= (int)sizeof(command)) {
        cached_branch[0] = '\0';
        return false;
    }
    pipe = popen(command, "r");
    if (pipe == NULL) {
        cached_branch[0] = '\0';
        return false;
    }
    if (fgets(cached_branch, sizeof(cached_branch), pipe) == NULL) {
        cached_branch[0] = '\0';
        pclose(pipe);
        return false;
    }
    pclose(pipe);
    size = strlen(cached_branch);
    while (size > 0 && (cached_branch[size - 1] == '\n' ||
                        cached_branch[size - 1] == '\r')) {
        cached_branch[--size] = '\0';
    }
    return cached_branch[0] != '\0';
}

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                   */
/* ------------------------------------------------------------------ */

static const struct telos_tui_keybinding keybindings[] = {
    {
        .sequence = "\033b",
        .description = "Toggle git branch display",
        .handler = keybinding_handler,
        .context = NULL,
    },
};

static const struct telos_tui_footer_section footer_sections[] = {
    {
        .id = "git-branch",
        .render = footer_render,
        .context = NULL,
    },
};

static const struct telos_tui_event_hook event_hooks[] = {
    {
        .id = "git-branch-refresh",
        .on_event = on_turn_event,
        .context = NULL,
    },
};

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.git-branch-status",
    .panels = NULL,
    .panel_count = 0,
    .keybindings = keybindings,
    .keybinding_count =
        sizeof(keybindings) / sizeof(keybindings[0]),
    .footer_sections = footer_sections,
    .footer_section_count =
        sizeof(footer_sections) / sizeof(footer_sections[0]),
    .input_preprocessors = NULL,
    .input_preprocessor_count = 0,
    .event_hooks = event_hooks,
    .event_hook_count = sizeof(event_hooks) / sizeof(event_hooks[0]),
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const char *const capabilities[] = {
        "posix.exec",
    };
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.git-branch-status",
        .kind = TELOS_EXTENSION_TUI_PLUGIN,
        .required_capabilities = capabilities,
        .required_capability_count = 1,
        .implementation = &definition,
    };

    if (host == NULL || host->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar == NULL ||
        registrar->abi_version != TELOS_PLUGIN_ABI_VERSION ||
        registrar->add == NULL) {
        return 1;
    }

    /*
     * Attempt an initial branch read.  The plugin works even when git is
     * unavailable — it just hides the footer section.
     */
    working_directory = getenv("PWD");
    if (working_directory == NULL || working_directory[0] == '\0') {
        working_directory = ".";
    }
    refresh_branch();

    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
