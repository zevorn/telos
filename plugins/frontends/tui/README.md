# Terminal Frontend

`dev.zevorn.terminal-frontend` is the POSIX terminal adapter for interactive
Telos sessions on Linux and macOS. It follows Pi's main-screen model: completed
messages remain in the terminal scrollback while only the live response,
editor, and footer are redrawn.

This deliberately mirrors the interaction shape described by Pi's
[interactive-mode documentation](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/README.md)
and its
[TUI package](https://github.com/earendil-works/pi/blob/main/packages/tui/README.md),
while keeping the implementation in C and behind the Telos Frontend contract.

The Frontend owns raw terminal input, bounded multi-line editing, streaming
rendering, steer input, and cancellation. It receives each Agent turn through
the narrow `telos_frontend_turn_fn` interface and has no Provider-specific
knowledge.

The implementation uses ANSI terminal control sequences and POSIX terminal
interfaces. It does not require ncurses and is not linked into Zephyr builds.

The editor accepts UTF-8 text and bracketed paste. Enter submits, Ctrl+J or
Alt+Enter inserts a line, arrow keys move the cursor, and Esc requests
cooperative cancellation. Up to eight prompts can be submitted while a Turn is
active; the Agent consumes them at safe model/tool boundaries without
cancelling the current request. Inputs are limited to 16 KiB, the
cross-thread Event queue is fixed at 64 entries, and each steer text is limited
to 16 KiB.

`!command` executes a bounded shell command in the session working directory;
`!!command` feeds its output into the next Agent Turn. Ctrl+G opens the prompt
in `$VISUAL` or `$EDITOR`, Ctrl+L redraws the screen, and Tab completes a
command (matching commands are listed live above the editor, and repeated Tab
cycles through matches). `/resume` followed by Tab opens the same selector for
persisted sessions, showing their inferred or user-defined names; Enter resumes
the selected session. `/model` opens a model selector above the input
editor, with the current model marked, Up/Down navigation, Enter selection,
and Esc cancellation. The selected model is saved as the user default. `/copy`
uses OSC 52 when the terminal supports clipboard control. The Frontend also
accepts `/login PROVIDER`, `/login-status PROVIDER`, `/logout PROVIDER`,
`/model PROVIDER/MODEL`, `/thinking LEVEL`, `/status FIELDS`, and `/setting`
without
coupling provider-specific behavior into the terminal implementation.

Responses use a small terminal Markdown renderer for headings, lists, emphasis,
links, inline code, and fenced code. During a turn, tool calls are kept in a
single bounded panel with alternating dark backgrounds and a last-items view;
Ctrl+O toggles the expanded panel. Tool rows include a compact argument or
result summary when the Provider supplies one.
