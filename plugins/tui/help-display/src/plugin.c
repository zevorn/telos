#include <stdio.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/command.h>
#include <telos/tui_plugin.h>

/*
 * Help Display — TUI command-interceptor + overlay plugin
 *
 * Intercepts /help and shows a formatted overlay listing all available
 * commands, keybindings, and usage hints.  Replaces the built-in help
 * display in tui.c.
 */

/* ------------------------------------------------------------------ */
/* Overlay render                                                       */
/* ------------------------------------------------------------------ */

static size_t overlay_render(void *context, char *buffer,
                              size_t buffer_size, size_t columns)
{
    const struct telos_frontend_session *session = context;
    size_t used = 0;

    (void)columns;
    (void)buffer_size;

    if (session == NULL) {
        return 0;
    }

    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "Telos Commands\n"
                              "==============\n"
                              "\n");

    if (session->commands != NULL) {
        for (size_t i = 0; i < session->commands->count; ++i) {
            const struct telos_command *cmd =
                &session->commands->commands[i];
            const char *help =
                cmd->help != NULL ? cmd->help : "";

            used += (size_t)snprintf(buffer + used, buffer_size - used,
                                      "  /%-20s %s\n", cmd->name, help);
        }
    }
    if (session->command_help != NULL &&
        session->command_help[0] != '\0') {
        used += (size_t)snprintf(buffer + used, buffer_size - used,
                                  "\n%s\n", session->command_help);
    }
    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "\n"
                              "Keyboard Shortcuts\n"
                              "==================\n"
                              "  Enter              submit prompt\n"
                              "  Ctrl+J / Alt+Enter add newline\n"
                              "  Esc                cancel / clear\n"
                              "  Ctrl+G             open $EDITOR\n"
                              "  Ctrl+O             toggle tool panel\n"
                              "  Ctrl+L             redraw screen\n"
                              "  Ctrl+C             cancel turn or exit\n"
                              "  Tab                complete command\n"
                              "  ↑ / ↓             recall last prompt\n"
                              "  PgUp / PgDn        scroll history\n"
                              "  Terminal scrollbar view earlier frames\n"
                              "  !command           run shell command\n"
                              "  !!command          run and send output\n"
                              "\n"
                              "Esc or Enter to close\n");

    /* Count rows. */
    {
        size_t rows = 0;
        for (size_t i = 0; i < used; ++i) {
            if (buffer[i] == '\n') {
                ++rows;
            }
        }
        return rows;
    }
}

/* ------------------------------------------------------------------ */
/* Overlay input                                                        */
/* ------------------------------------------------------------------ */

static bool overlay_input(void *context, const char *data, size_t size,
                           bool *consumed, struct telos_error **error)
{
    (void)context;
    (void)size;
    (void)error;

    if (data[0] == '\r' || data[0] == '\n' || data[0] == '\033' ||
        (unsigned char)data[0] == 0x03U) {
        *consumed = true;
        return true;
    }
    *consumed = false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Overlay close                                                        */
/* ------------------------------------------------------------------ */

static void overlay_close(void *context)
{
    (void)context;
}

/* ------------------------------------------------------------------ */
/* Command interceptor                                                  */
/* ------------------------------------------------------------------ */

static bool intercept_help(void *context, const char *line,
                           struct telos_tui_host *host,
                           struct telos_error **error)
{
    const struct telos_frontend_session *session;

    (void)context;
    (void)line;
    (void)error;

    if (host == NULL) {
        return false;
    }
    session = telos_tui_host_session(host);
    if (session == NULL) {
        return false;
    }
    return telos_tui_host_activate_overlay(host, "help-display",
                                           (void *)session, NULL);
}

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                   */
/* ------------------------------------------------------------------ */

static const struct telos_tui_overlay overlays[] = {
    {
        .id = "help-display",
        .render = overlay_render,
        .handle_input = overlay_input,
        .on_close = overlay_close,
        .context = NULL,
    },
};

static const struct telos_tui_command_interceptor interceptors[] = {
    {
        .id = "help-interceptor",
        .prefix = "/help",
        .handle = intercept_help,
        .context = NULL,
    },
};

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.help-display",
    .panels = NULL,
    .panel_count = 0,
    .overlays = overlays,
    .overlay_count = sizeof(overlays) / sizeof(overlays[0]),
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
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.help-display",
        .kind = TELOS_EXTENSION_TUI_PLUGIN,
        .required_capabilities = NULL,
        .required_capability_count = 0,
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
