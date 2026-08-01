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
rendering, queued input, and cancellation. It receives each Agent turn through
the narrow `telos_frontend_turn_fn` interface and has no Provider-specific
knowledge.

The implementation uses ANSI terminal control sequences and POSIX terminal
interfaces. It does not require ncurses and is not linked into Zephyr builds.

The editor accepts UTF-8 text and bracketed paste. Enter submits, Ctrl+J or
Alt+Enter inserts a line, arrow keys move the cursor, and Esc requests
cooperative cancellation. One prompt may be queued while a Turn is active.
Inputs are limited to 16 KiB, the cross-thread Event queue is fixed at 64
entries, and each queued text fragment is limited to 2 KiB.
