#ifndef TELOS_TUI_PLUGIN_H
#define TELOS_TUI_PLUGIN_H

#include <telos/types.h>

#include <telos/error.h>
#include <telos/frontend.h>

/*
 * TUI Plugin Extension Point
 *
 * Plugins register a telos_tui_plugin_definition_v1 to extend the TUI
 * frontend without coupling feature code into the TUI shell.
 *
 * A TUI plugin can contribute any combination of:
 *   - Panels:              rendered sections at fixed layout positions
 *   - Overlays:            modal dialogs that own input while active
 *   - Keybindings:         custom keyboard shortcuts with descriptions
 *   - Footer sections:     dynamic text contributors to the status bar
 *   - Input preprocessors: transform user input before it is dispatched
 *   - Event hooks:         observe frontend events as they are emitted
 *   - Completion providers:contribute tab-completion items
 *   - Command interceptors:handle lines starting with a specific prefix
 *
 * The TUI shell owns lifecycle; plugin callbacks must be re-entrant and
 * must not block or call into the TUI shell recursively.
 */

#define TELOS_TUI_PLUGIN_PANEL_RENDER_SIZE 4096U
#define TELOS_TUI_PLUGIN_FOOTER_RENDER_SIZE 256U
#define TELOS_TUI_PLUGIN_KEY_SEQUENCE_SIZE 16U
#define TELOS_TUI_PLUGIN_OVERLAY_RENDER_SIZE 8192U
#define TELOS_TUI_PLUGIN_COMPLETION_VALUE_SIZE 256U
#define TELOS_TUI_PLUGIN_COMPLETION_DETAIL_SIZE 512U

/* ------------------------------------------------------------------ */
/* Panel                                                                */
/* ------------------------------------------------------------------ */

enum telos_tui_panel_position {
    TELOS_TUI_PANEL_ABOVE_EDITOR = 1,
    TELOS_TUI_PANEL_BELOW_EDITOR,
};

/*
 * Render this panel into buffer (at most buffer_size bytes, always
 * NUL-terminated).  Return the number of terminal rows consumed, or 0
 * when the panel is empty.  Called on every render frame.
 */
typedef size_t (*telos_tui_panel_render_fn)(void *context,
                                            char *buffer,
                                            size_t buffer_size,
                                            size_t columns);

/*
 * Offer keyboard input to this panel while it is visible.  Set *consumed
 * to true when the panel handled the input and the TUI shell should skip
 * default processing.  data/size is the raw terminal sequence.
 */
typedef bool (*telos_tui_panel_input_fn)(void *context,
                                         const char *data,
                                         size_t size,
                                         bool *consumed,
                                         struct telos_error **error);

struct telos_tui_panel {
    const char *id;
    enum telos_tui_panel_position position;
    telos_tui_panel_render_fn render;
    telos_tui_panel_input_fn handle_input;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Keybinding                                                           */
/* ------------------------------------------------------------------ */

struct telos_tui_host;

/*
 * Handler returns true on success.  The TUI shell stops dispatching the
 * current input sequence after the first handler returns true.
 * The host parameter allows the handler to activate overlays or query
 * viewport state.
 */
typedef bool (*telos_tui_keybinding_handler_fn)(void *context,
                                                struct telos_tui_host *host,
                                                struct telos_error **error);

struct telos_tui_keybinding {
    char sequence[TELOS_TUI_PLUGIN_KEY_SEQUENCE_SIZE];
    const char *description;
    telos_tui_keybinding_handler_fn handler;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Footer section                                                       */
/* ------------------------------------------------------------------ */

/*
 * Render a footer segment into buffer.  Return the number of bytes
 * written (not including NUL).  Return 0 to hide this section.
 * Called on every render frame.
 */
typedef size_t (*telos_tui_footer_render_fn)(void *context,
                                             char *buffer,
                                             size_t buffer_size);

struct telos_tui_footer_section {
    const char *id;
    telos_tui_footer_render_fn render;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Input preprocessor                                                   */
/* ------------------------------------------------------------------ */

/*
 * Called before the TUI shell processes a submitted line.  The plugin may
 * transform the input in-place and update input_size and cursor.
 * Return false to suppress the turn entirely (the input is discarded).
 */
typedef bool (*telos_tui_input_preprocess_fn)(void *context,
                                              char *input,
                                              size_t *input_size,
                                              size_t *cursor);

struct telos_tui_input_preprocessor {
    const char *id;
    telos_tui_input_preprocess_fn preprocess;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Event hook                                                           */
/* ------------------------------------------------------------------ */

/*
 * Called synchronously for every frontend event, before the TUI shell
 * processes it.  Plugins must not block or queue additional events from
 * this callback.
 */
typedef void (*telos_tui_event_hook_fn)(void *context,
                                        const struct telos_frontend_event *event);

struct telos_tui_event_hook {
    const char *id;
    telos_tui_event_hook_fn on_event;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Overlay                                                              */
/* ------------------------------------------------------------------ */

/*
 * Modal overlay that takes over the TUI while active.  Only one overlay
 * can be active at a time; the TUI shell serializes activation.
 *
 * Render the overlay content into buffer (newline-separated rows).
 * Return the number of rows consumed, or 0 when hidden.
 */
typedef size_t (*telos_tui_overlay_render_fn)(void *context,
                                              char *buffer,
                                              size_t buffer_size,
                                              size_t columns);

/*
 * Handle keyboard input while this overlay owns focus.  Set *consumed
 * to true when the overlay handled the input.  Return false on error.
 */
typedef bool (*telos_tui_overlay_input_fn)(void *context,
                                           const char *data,
                                           size_t size,
                                           bool *consumed,
                                           struct telos_error **error);

/*
 * Called when the overlay is closed (by user action, plugin request,
 * or TUI shutdown).  Plugins release resources here.
 */
typedef void (*telos_tui_overlay_close_fn)(void *context);

/*
 * Opaque handle the plugin uses to interact with the TUI shell.
 * Obtained through the plugin registrar during initialization.
 */
struct telos_tui_host;

/*
 * Activate one of this plugin's overlays.  The overlay must be declared
 * in the plugin's definition.  Only one overlay may be active at a time;
 * returns false when another overlay is already active.
 */
bool telos_tui_host_activate_overlay(struct telos_tui_host *host,
                                     const char *overlay_id,
                                     void *context,
                                     struct telos_error **error);

/* Close the currently active overlay if it belongs to this host. */
bool telos_tui_host_close_overlay(struct telos_tui_host *host);

/* Current terminal columns. */
size_t telos_tui_host_columns(const struct telos_tui_host *host);

/* Current terminal rows. */
size_t telos_tui_host_rows(const struct telos_tui_host *host);

/*
 * Return the frontend session this TUI is driving.  Plugins may read
 * model catalog, commands, status, and identity from the session but
 * must not mutate it or call turn() directly.
 */
const struct telos_frontend_session *
telos_tui_host_session(const struct telos_tui_host *host);

/*
 * Submit a line as if the user typed it into the editor.  The line is
 * processed through normal command / turn dispatch.  Returns false when
 * the TUI is shutting down.
 */
bool telos_tui_host_submit(struct telos_tui_host *host,
                           const char *line,
                           struct telos_error **error);

/*
 * Display a transient notice to the user (like a frontend NOTICE event).
 * The notice appears in the history area and does not start a turn.
 */
bool telos_tui_host_notice(struct telos_tui_host *host,
                           const char *text,
                           struct telos_error **error);

struct telos_tui_overlay {
    const char *id;
    telos_tui_overlay_render_fn render;
    telos_tui_overlay_input_fn handle_input;
    telos_tui_overlay_close_fn on_close;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Completion provider                                                  */
/* ------------------------------------------------------------------ */

struct telos_tui_completion_item {
    char value[TELOS_TUI_PLUGIN_COMPLETION_VALUE_SIZE];
    char detail[TELOS_TUI_PLUGIN_COMPLETION_DETAIL_SIZE];
};

/*
 * Return the number of completions for the given input prefix.
 * Return 0 when no completions are available.
 */
typedef size_t (*telos_tui_completion_count_fn)(void *context,
                                                const char *prefix);

/*
 * Fill item for the given ordinal (0-based).  Return false when
 * ordinal is out of range.
 */
typedef bool (*telos_tui_completion_at_fn)(void *context,
                                           const char *prefix,
                                           size_t ordinal,
                                           struct telos_tui_completion_item *item);

struct telos_tui_completion_provider {
    const char *id;
    telos_tui_completion_count_fn count;
    telos_tui_completion_at_fn at;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Command interceptor                                                  */
/* ------------------------------------------------------------------ */

/*
 * Intercept a submitted line before normal command / turn dispatch.
 * When a user submits a line starting with `prefix`, the TUI shell calls
 * `handle` instead of processing the line itself.
 *
 * Return true if the interceptor consumed the line (the TUI should skip
 * default processing).  Return false to fall through to normal dispatch.
 */
typedef bool (*telos_tui_command_intercept_fn)(void *context,
                                               const char *line,
                                               struct telos_tui_host *host,
                                               struct telos_error **error);

struct telos_tui_command_interceptor {
    const char *id;
    const char *prefix;
    telos_tui_command_intercept_fn handle;
    void *context;
};

/* ------------------------------------------------------------------ */
/* Plugin definition                                                    */
/* ------------------------------------------------------------------ */

struct telos_tui_plugin_definition_v1 {
    uint32_t struct_size;
    const char *id;
    const struct telos_tui_panel *panels;
    size_t panel_count;
    const struct telos_tui_overlay *overlays;
    size_t overlay_count;
    const struct telos_tui_keybinding *keybindings;
    size_t keybinding_count;
    const struct telos_tui_footer_section *footer_sections;
    size_t footer_section_count;
    const struct telos_tui_input_preprocessor *input_preprocessors;
    size_t input_preprocessor_count;
    const struct telos_tui_event_hook *event_hooks;
    size_t event_hook_count;
    const struct telos_tui_completion_provider *completion_providers;
    size_t completion_provider_count;
    const struct telos_tui_command_interceptor *command_interceptors;
    size_t command_interceptor_count;
};

#endif
