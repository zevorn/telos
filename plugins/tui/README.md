# TUI Plugins

TUI plugins extend the Telos terminal frontend without coupling feature code
into the TUI shell.  This follows the **"everything is a plugin"** design:
the TUI shell handles only raw-mode I/O and frame rendering; all behaviour
is contributed by plugins.

## Quick Start

Copy `sdk/templates/tui-plugin/` and rename `MY-PLUGIN` in `plugin.toml`.
Implement one or more extension points, then add your plugin to
`plugins/tui/meson.build` and `plugins/meson.build`.

## Extension Points

A plugin registers a `telos_tui_plugin_definition_v1` containing any
combination of these eight extension points:

| Extension point       | Struct type                        | Use when you want to… |
|-----------------------|------------------------------------|------------------------|
| **panels**            | `telos_tui_panel`                  | show persistent content above/below the editor |
| **overlays**          | `telos_tui_overlay`                | show a modal dialog that owns input while active |
| **keybindings**       | `telos_tui_keybinding`             | react to a keyboard shortcut |
| **footer_sections**   | `telos_tui_footer_section`         | add dynamic text to the status bar |
| **input_preprocessors**| `telos_tui_input_preprocessor`    | transform or suppress a submitted line |
| **event_hooks**       | `telos_tui_event_hook`             | observe frontend events (tool calls, text, etc.) |
| **completion_providers**| `telos_tui_completion_provider`  | contribute tab-completion items |
| **command_interceptors**| `telos_tui_command_interceptor`  | handle lines starting with a specific prefix |

## TUI Host API

Keybinding handlers and command interceptors receive a `struct telos_tui_host *`
that provides:

| Function | Purpose |
|----------|---------|
| `telos_tui_host_session()` | access the frontend session (commands, model catalog, status) |
| `telos_tui_host_submit()` | submit text as if the user typed it |
| `telos_tui_host_notice()` | show a transient notice in the history |
| `telos_tui_host_activate_overlay()` | open one of your declared overlays |
| `telos_tui_host_close_overlay()` | close the currently active overlay |
| `telos_tui_host_columns()` | terminal width in columns |
| `telos_tui_host_rows()` | terminal height in rows |

## Bundled Plugins

| Plugin | Extension points used | What it does |
|--------|----------------------|--------------|
| `git-branch-status` | footer, keybinding, event_hook | shows current git branch in the status bar |
| `keybinding-help` | overlay, keybinding | Alt+? shows all registered keybindings |
| `model-selector` | overlay, keybinding | Ctrl+M or /model opens model picker |
| `shell-exec` | command_interceptor | !command and !!command shell execution |
| `help-display` | overlay, command_interceptor | /help shows commands and shortcuts |

## Design Principles

1. **Plugins own their state.**  The TUI shell does not inspect plugin state;
   each plugin manages its own context pointer.

2. **Callbacks are re-entrant.**  The TUI shell may call render and input
   handlers from the main loop; plugins must not block or call back into
   the TUI recursively.

3. **Capabilities are declared.**  Plugins list required capabilities in
   `plugin.toml`; the host enforces them before loading.

4. **Overlays are modal.**  Only one overlay can be active at a time.
   While active, the overlay receives all keyboard input and replaces the
   normal TUI rendering.

5. **Interceptors run before built-ins.**  Command interceptors are tried
   before the TUI's built-in command handling, so plugins can override
   defaults.
