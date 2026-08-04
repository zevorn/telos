#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/tui_plugin.h>

/*
 * Shell Execution — TUI command-interceptor plugin
 *
 * Intercepts lines starting with "!" and executes them as shell commands.
 *
 *   !command   — run command, show output as a notice
 *   !!command  — run command, feed output back as the next prompt
 *
 * Replaces the built-in shell execution in tui.c.
 */

#define SHELL_OUTPUT_SIZE (256U * 1024U)
#define SHELL_TIMEOUT_SECONDS 30

/* ------------------------------------------------------------------ */
/* Command interceptor                                                  */
/* ------------------------------------------------------------------ */

static bool intercept_shell(void *context, const char *line,
                            struct telos_tui_host *host,
                            struct telos_error **error)
{
    const bool capture = line[1] == '!';
    const char *command = line + (capture ? 2 : 1);
    const char *shell;
    char *output;
    FILE *pipe;
    size_t used = 0;
    size_t chunk;

    (void)context;
    (void)error;

    if (command[0] == '\0') {
        telos_tui_host_notice(host, "Usage: !command or !!command", NULL);
        return true;
    }

    shell = getenv("TELOS_AGENT_SHELL");
    if (shell == NULL || shell[0] == '\0') {
        shell = getenv("SHELL");
    }
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/sh";
    }

    output = malloc(SHELL_OUTPUT_SIZE);
    if (output == NULL) {
        telos_tui_host_notice(host, "Shell: allocation failed", NULL);
        return true;
    }

    {
        char cmd[1024];

        if (snprintf(cmd, sizeof(cmd), "%s -c '%s' 2>&1", shell,
                     command) >= (int)sizeof(cmd)) {
            telos_tui_host_notice(host, "Shell: command too long", NULL);
            free(output);
            return true;
        }
        pipe = popen(cmd, "r");
    }

    if (pipe == NULL) {
        telos_tui_host_notice(host, "Shell: could not start", NULL);
        free(output);
        return true;
    }

    while (used + 1 < SHELL_OUTPUT_SIZE &&
           (chunk = fread(output + used, 1,
                          SHELL_OUTPUT_SIZE - 1 - used, pipe)) > 0) {
        used += chunk;
    }
    pclose(pipe);
    output[used] = '\0';

    /* Trim trailing newlines. */
    while (used > 0 && (output[used - 1] == '\n' || output[used - 1] == '\r')) {
        output[--used] = '\0';
    }

    if (capture && output[0] != '\0') {
        /* Feed output back as the next prompt. */
        telos_tui_host_submit(host, output, NULL);
    } else if (output[0] != '\0') {
        telos_tui_host_notice(host, output, NULL);
    }
    free(output);
    return true;
}

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                   */
/* ------------------------------------------------------------------ */

static const struct telos_tui_command_interceptor interceptors[] = {
    {
        .id = "shell-exec",
        .prefix = "!",
        .handle = intercept_shell,
        .context = NULL,
    },
};

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.shell-exec",
    .panels = NULL,
    .panel_count = 0,
    .overlays = NULL,
    .overlay_count = 0,
    .keybindings = NULL,
    .keybinding_count = 0,
    .footer_sections = NULL,
    .footer_section_count = 0,
    .input_preprocessors = NULL,
    .input_preprocessor_count = 0,
    .event_hooks = NULL,
    .event_hook_count = 0,
    .completion_providers = NULL,
    .completion_provider_count = 0,
    .command_interceptors = interceptors,
    .command_interceptor_count =
        sizeof(interceptors) / sizeof(interceptors[0]),
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const char *const capabilities[] = {
        "posix.exec",
    };
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.shell-exec",
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
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
