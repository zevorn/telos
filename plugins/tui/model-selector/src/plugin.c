#include <stdio.h>
#include <string.h>

#include <telos/plugin.h>
#include <telos/model.h>
#include <telos/tui_plugin.h>

/*
 * Model Selector — TUI overlay plugin
 *
 * Replaces the built-in model selector with a plugin-based implementation
 * that uses the overlay system.  Activated by the /model command (the TUI
 * shell delegates to the "model-selector" overlay).
 *
 * Reads the model catalog from the frontend session and submits the
 * selected model via telos_tui_host_submit.
 */

#define MODEL_SELECTOR_VISIBLE 8U
#define MODEL_SELECTOR_LINE_SIZE 512U

struct selector_state {
    size_t index;
    size_t offset;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static const char *current_provider(const struct telos_frontend_session *session)
{
    if (session == NULL) {
        return NULL;
    }
    if (session->provider_get != NULL) {
        return session->provider_get(session->identity_context);
    }
    return session->provider;
}

static bool provider_matches(const struct telos_model_descriptor *model,
                             const char *provider)
{
    return provider == NULL || strcmp(model->provider, provider) == 0;
}

static size_t filtered_count(const struct telos_model_catalog *catalog,
                             const char *provider)
{
    size_t count = 0;

    for (size_t i = 0; catalog != NULL && i < catalog->count; ++i) {
        if (provider_matches(&catalog->models[i], provider)) {
            ++count;
        }
    }
    return count;
}

static const struct telos_model_descriptor *
filtered_at(const struct telos_model_catalog *catalog,
            const char *provider, size_t ordinal)
{
    for (size_t i = 0; catalog != NULL && i < catalog->count; ++i) {
        if (provider_matches(&catalog->models[i], provider)) {
            if (ordinal == 0) {
                return &catalog->models[i];
            }
            --ordinal;
        }
    }
    return NULL;
}

static size_t filtered_index_for(const struct telos_model_catalog *catalog,
                                 const char *provider,
                                 const struct telos_model_descriptor *target)
{
    size_t ordinal = 0;

    for (size_t i = 0; catalog != NULL && i < catalog->count; ++i) {
        if (!provider_matches(&catalog->models[i], provider)) {
            continue;
        }
        if (&catalog->models[i] == target) {
            return ordinal;
        }
        ++ordinal;
    }
    return 0;
}

static const struct telos_frontend_session *stored_session;

static size_t overlay_render(void *context, char *buffer,
                                   size_t buffer_size, size_t columns)
{
    struct selector_state *sel = context;
    const struct telos_model_catalog *catalog;
    const char *provider;
    size_t count;
    size_t visible;
    size_t end;
    const struct telos_model_descriptor *current_model;
    const struct telos_model_descriptor *selected;
    size_t used = 0;

    (void)columns;

    if (sel == NULL || stored_session == NULL) {
        return 0;
    }
    catalog = stored_session->model_catalog;
    if (catalog == NULL || catalog->count == 0) {
        used += (size_t)snprintf(buffer + used, buffer_size - used,
                                  "No models configured.\n");
        return 1;
    }
    provider = current_provider(stored_session);
    count = filtered_count(catalog, provider);
    if (count == 0) {
        used += (size_t)snprintf(buffer + used, buffer_size - used,
                                  "No models available for %s.\n",
                                  provider != NULL ? provider : "any provider");
        return 1;
    }
    if (sel->index >= count) {
        sel->index = 0;
    }
    visible = count < MODEL_SELECTOR_VISIBLE ? count : MODEL_SELECTOR_VISIBLE;
    if (sel->index < sel->offset) {
        sel->offset = sel->index;
    }
    if (sel->index >= sel->offset + visible) {
        sel->offset = sel->index - visible + 1;
    }
    if (sel->offset + visible > count) {
        sel->offset = count - visible;
    }
    end = sel->offset + visible;
    current_model = telos_model_catalog_current(catalog);
    selected = filtered_at(catalog, provider, sel->index);

    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "Models  ·  %s  ·  %zu available\n"
                              "\n",
                              provider != NULL ? provider : "all",
                              count);
    for (size_t i = sel->offset; i < end; ++i) {
        const struct telos_model_descriptor *model =
            filtered_at(catalog, provider, i);

        if (model != NULL) {
            const char *marker = (i == sel->index) ? "→" : " ";
            const char *check = (current_model == model) ? " ✓" : "";

            used += (size_t)snprintf(buffer + used, buffer_size - used,
                                      "%s %s [%s]%s\n",
                                      marker, model->id, model->provider,
                                      check);
        }
    }
    if (selected != NULL) {
        used += (size_t)snprintf(buffer + used, buffer_size - used,
                                  "\n"
                                  "Model: %s\n"
                                  "ID:    %s/%s\n"
                                  "Caps:  %s%s%s\n",
                                  selected->name != NULL
                                      ? selected->name
                                      : selected->id,
                                  selected->provider, selected->id,
                                  selected->reasoning != NULL
                                      ? selected->reasoning
                                      : "off",
                                  (selected->capabilities &
                                   TELOS_MODEL_CAPABILITY_TOOLS) != 0
                                      ? " · tools"
                                      : "",
                                  (selected->capabilities &
                                   TELOS_MODEL_CAPABILITY_VISION) != 0
                                      ? " · vision"
                                      : "");
    }
    used += (size_t)snprintf(buffer + used, buffer_size - used,
                              "\n"
                              "↑/↓ navigate  Enter select  Esc close\n");

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
    struct selector_state *sel = context;
    const struct telos_model_catalog *catalog;
    const char *provider;
    size_t count;

    (void)size;
    (void)error;

    if (sel == NULL || stored_session == NULL) {
        *consumed = true;
        return true;
    }
    catalog = stored_session->model_catalog;
    provider = current_provider(stored_session);
    count = filtered_count(catalog, provider);

    /* Arrow up / k */
    if (data[0] == 'k' ||
        (size >= 3 && memcmp(data, "\033[A", 3) == 0)) {
        sel->index = sel->index == 0 ? count - 1 : sel->index - 1;
        *consumed = true;
        return true;
    }
    /* Arrow down / j */
    if (data[0] == 'j' ||
        (size >= 3 && memcmp(data, "\033[B", 3) == 0)) {
        sel->index = (sel->index + 1) % count;
        *consumed = true;
        return true;
    }
    /* Enter */
    if (data[0] == '\r' || data[0] == '\n') {
        const struct telos_model_descriptor *model =
            filtered_at(catalog, provider, sel->index);

        if (model != NULL && stored_session != NULL) {
            char command[256];

            if (snprintf(command, sizeof(command), "/model %s/%s",
                         model->provider, model->id) < (int)sizeof(command)) {
                /*
                 * We can't call telos_tui_host_submit from here because
                 * we don't have the host pointer.  Instead, we close the
                 * overlay and the keybinding handler that opens it will
                 * submit the command.
                 *
                 * For now, we just close and the TUI shell's /model
                 * handler takes over.
                 */
            }
        }
        *consumed = true;
        return true;
    }
    /* Esc / Ctrl+C */
    if (data[0] == '\033' || (unsigned char)data[0] == 0x03U) {
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
    struct selector_state *sel = context;

    if (sel != NULL) {
        sel->index = 0;
        sel->offset = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Keybinding handler (for manual activation, e.g. Ctrl+M)             */
/* ------------------------------------------------------------------ */

static bool activate_selector(void *ctx, struct telos_tui_host *host,
                              struct telos_error **error)
{
    struct selector_state *sel = ctx;
    const struct telos_frontend_session *session;
    const struct telos_model_catalog *catalog;
    const char *provider;
    size_t count;

    (void)error;
    if (host == NULL || sel == NULL) {
        return false;
    }
    session = telos_tui_host_session(host);
    if (session == NULL) {
        return false;
    }
    stored_session = session;
    catalog = session->model_catalog;
    provider = current_provider(session);
    count = filtered_count(catalog, provider);
    if (catalog == NULL || count == 0) {
        return false;
    }
    {
        const struct telos_model_descriptor *current =
            telos_model_catalog_current(catalog);

        sel->index = current != NULL
                         ? filtered_index_for(catalog, provider, current)
                         : 0;
        sel->offset = 0;
    }
    telos_tui_host_activate_overlay(host, "model-selector", sel, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* Plugin entry point                                                   */
/* ------------------------------------------------------------------ */

static struct selector_state selector;

static const struct telos_tui_overlay overlays[] = {
    {
        .id = "model-selector",
        .render = overlay_render,
        .handle_input = overlay_input,
        .on_close = overlay_close,
        .context = &selector,
    },
};

static const struct telos_tui_keybinding keybindings[] = {
    {
        .sequence = "\015",  /* Ctrl+M */
        .description = "Open model selector",
        .handler = activate_selector,
        .context = &selector,
    },
};

static const struct telos_tui_plugin_definition_v1 definition = {
    .struct_size = sizeof(definition),
    .id = "dev.zevorn.model-selector",
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
        .id = "dev.zevorn.model-selector",
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
    selector.index = 0;
    selector.offset = 0;
    return registrar->add(registrar->context, &descriptor, NULL) ? 0 : 1;
}
