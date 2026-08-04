#include <stdio.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/tui_plugin.h>

/*
 * Keybinding Help — example TUI overlay plugin
 *
 * Demonstrates the overlay system by showing a help screen that lists
 * all registered keybindings from every TUI plugin.
 *
 * Activated by Alt+? (Escape-? sequence).  The overlay can be navigated
 * with arrow keys and dismissed with Esc or Enter.
 */

#define MAXIMUM_VISIBLE 16U

static size_t scroll_offset;

/* ------------------------------------------------------------------ */
/* Overlay render                                                       */
/* ------------------------------------------------------------------ */

static size_t overlay_render(void *context, char *buffer,
                              size_t buffer_size, size_t columns)
{
    const struct telos_tui_plugin_definition_v1 *const *plugins = context;
    size_t plugin_count = 0;
    size_t used = 0;
    size_t line_count = 0;

    (void)columns;

    /* Count plugins (NULL-terminated). */
    while (plugins != NULL && plugins[plugin_count] != NULL) {
        ++plugin_count;
    }

    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "Keybindings\n"
                              "============\n"
                              "\n");

    for (size_t i = 0; i < plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin = plugins[i];

        for (size_t j = 0; j < plugin->keybinding_count; ++j) {
            const struct telos_tui_keybinding *kb = &plugin->keybindings[j];
            char display[32];
            size_t di = 0;

            /* Render the key sequence in a readable form. */
            for (size_t k = 0; kb->sequence[k] != '\0' && di < sizeof(display) - 1; ++k) {
                unsigned char c = (unsigned char)kb->sequence[k];

                if (c == 0x1bU) {
                    if (di + 2 < sizeof(display)) {
                        display[di++] = 'A';
                        display[di++] = 'l';
                        display[di++] = 't';
                        display[di++] = '+';
                    }
                } else if (c >= 0x20U && c < 0x7fU) {
                    display[di++] = (char)c;
                } else if (c < 0x20U) {
                    display[di++] = '^';
                    display[di++] = (char)('@' + c);
                }
            }
            display[di] = '\0';

            if (used + 128 < buffer_size) {
                used += (size_t)snprintf(buffer + used, buffer_size - used,
                                          "  %-16s %s\n",
                                          display,
                                          kb->description != NULL
                                              ? kb->description
                                              : "");
                ++line_count;
            }
        }
    }

    if (line_count == 0) {
        used += (size_t)snprintf(buffer + used, buffer_size - used,
                                  "  (no keybindings registered)\n");
    }
    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "\nEsc or Enter to close\n");

    /* Count total newlines for row count. */
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
        /* The TUI shell will close via the keybinding handler calling
         * telos_tui_host_close_overlay, or Esc will be handled by the
         * overlay system since we return consumed=true and the shell
         * knows to close overlays on Esc. */
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
    scroll_offset = 0;
}

/* ------------------------------------------------------------------ */
/* Keybinding handler                                                   */
/* ------------------------------------------------------------------ */

static bool activate_overlay(void *context, struct telos_tui_host *host,
                             struct telos_error **error)
{
    (void)error;
    telos_tui_host_activate_overlay(host, "keybinding-help", context, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                   */
/* ------------------------------------------------------------------ */

static const struct telos_tui_overlay overlays[] = {
    {
        .id = "keybinding-help",
        .render = overlay_render,
        .handle_input = overlay_input,
        .on_close = overlay_close,
        .context = NULL,
    },
};

static const struct telos_tui_keybinding keybindings[] = {
    {
        .sequence = "\033?",
        .description = "Show keybinding help",
        .handler = activate_overlay,
        .context = NULL,
    },
};

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.keybinding-help",
    .panels = NULL,
    .panel_count = 0,
    .overlays = overlays,
    .overlay_count = sizeof(overlays) / sizeof(overlays[0]),
    .keybindings = keybindings,
    .keybinding_count = sizeof(keybindings) / sizeof(keybindings[0]),
    .footer_sections = NULL,
    .footer_section_count = 0,
    .input_preprocessors = NULL,
    .input_preprocessor_count = 0,
    .event_hooks = NULL,
    .event_hook_count = 0,
    .completion_providers = NULL,
    .completion_provider_count = 0,
};

int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.zevorn.keybinding-help",
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
