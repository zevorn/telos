/*
 * My TUI Plugin — skeleton / template
 *
 * Copy this directory, rename MY-PLUGIN in plugin.toml, and implement
 * your feature using one or more TUI extension points:
 *
 *   Extension point     | Use when you want to…
 *   --------------------+--------------------------------------------------
 *   panels              | show persistent content above/below the editor
 *   overlays            | show a modal dialog that owns input while active
 *   keybindings         | react to a keyboard shortcut
 *   footer_sections     | add dynamic text to the status bar
 *   input_preprocessors | transform or suppress a submitted line
 *   event_hooks         | observe frontend events (tool calls, text, etc.)
 *   completion_providers| contribute tab-completion items
 *   command_interceptors| handle lines starting with a specific prefix
 *
 * The TUI host (struct telos_tui_host *) gives you access to:
 *   - telos_tui_host_session()       → frontend session (commands, models)
 *   - telos_tui_host_submit()        → submit text as if the user typed it
 *   - telos_tui_host_notice()        → show a transient notice
 *   - telos_tui_host_activate_overlay() → open one of your overlays
 *   - telos_tui_host_close_overlay() → close the active overlay
 *   - telos_tui_host_columns/rows()  → terminal viewport size
 */

#include <stdio.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/tui_plugin.h>

/* ------------------------------------------------------------------ */
/* Extension point implementations (pick what you need)                 */
/* ------------------------------------------------------------------ */

#if 0  /* Example: keybinding */
static bool my_keybinding(void *context, struct telos_tui_host *host,
                          struct telos_error **error)
{
    (void)context;
    (void)error;
    telos_tui_host_notice(host, "Keybinding triggered!", NULL);
    return true;
}
#endif

#if 0  /* Example: footer section */
static size_t my_footer(void *context, char *buffer, size_t buffer_size)
{
    (void)context;
    return (size_t)snprintf(buffer, buffer_size, "my-info");
}
#endif

#if 0  /* Example: overlay */
static size_t my_overlay_render(void *context, char *buffer,
                                 size_t buffer_size, size_t columns)
{
    (void)context;
    (void)columns;
    return (size_t)snprintf(buffer, buffer_size,
                            "My Overlay\n"
                            "==========\n"
                            "\n"
                            "Esc or Enter to close\n");
}

static bool my_overlay_input(void *context, const char *data, size_t size,
                              bool *consumed, struct telos_error **error)
{
    (void)context;
    (void)size;
    (void)error;
    if (data[0] == '\r' || data[0] == '\n' || data[0] == '\033') {
        *consumed = true;
        return true;
    }
    *consumed = false;
    return true;
}

static void my_overlay_close(void *context)
{
    (void)context;
    /* Release resources here. */
}
#endif

#if 0  /* Example: command interceptor */
static bool my_interceptor(void *context, const char *line,
                           struct telos_tui_host *host,
                           struct telos_error **error)
{
    (void)context;
    (void)line;
    (void)error;
    telos_tui_host_notice(host, "Intercepted!", NULL);
    return true;  /* true = handled, skip default dispatch */
}
#endif

/* ------------------------------------------------------------------ */
/* Plugin definition — list only the extension points you implement    */
/* ------------------------------------------------------------------ */

/*
 * Uncomment and fill in the arrays you need, then reference them in
 * the definition struct below.
 */

/*
static const struct telos_tui_keybinding my_keybindings[] = {
    {
        .sequence = "\033x",    // Alt+X
        .description = "My keybinding",
        .handler = my_keybinding,
        .context = NULL,
    },
};
*/

/*
static const struct telos_tui_footer_section my_footers[] = {
    {
        .id = "my-footer",
        .render = my_footer,
        .context = NULL,
    },
};
*/

/*
static const struct telos_tui_overlay my_overlays[] = {
    {
        .id = "my-overlay",
        .render = my_overlay_render,
        .handle_input = my_overlay_input,
        .on_close = my_overlay_close,
        .context = NULL,
    },
};
*/

/*
static const struct telos_tui_command_interceptor my_interceptors[] = {
    {
        .id = "my-interceptor",
        .prefix = "?",
        .handle = my_interceptor,
        .context = NULL,
    },
};
*/

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.MY-PLUGIN",
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
    .command_interceptors = NULL,
    .command_interceptor_count = 0,
};

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                  */
/* ------------------------------------------------------------------ */

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const char *const capabilities[] = {
        /* List capabilities your plugin needs, e.g.: */
        /* "posix.exec", */
    };
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.MY-PLUGIN",
        .kind = TELOS_EXTENSION_TUI_PLUGIN,
        .required_capabilities = capabilities,
        .required_capability_count =
            sizeof(capabilities) / sizeof(capabilities[0]),
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
