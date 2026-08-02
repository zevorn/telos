# Terminal Agent on Linux and macOS

Telos provides a main-screen terminal Agent for Linux and macOS. Completed
messages stay in normal terminal scrollback; only the live response, editor,
and footer are redrawn. This preserves shell history and avoids a full-screen
ncurses dependency.

## Build

Install a C17 compiler, Meson, Ninja, pkg-config, and the libcurl development
package. Common package names are `libcurl4-openssl-dev` on Debian and Ubuntu,
`libcurl-devel` on Fedora, and `curl-dev` on Alpine.

```sh
meson setup build -Dcurl_transport=enabled
meson compile -C build
meson test -C build --print-errorlogs
```

`-Dcurl_transport=enabled` makes configuration fail early when libcurl is
missing. The default `auto` mode keeps minimal and Zephyr builds usable by
omitting the Transport when the dependency is unavailable.

## Start an interactive session

Start the terminal and sign in from inside the TUI:

```sh
build/tools/telos
```

Type `login` or `/login`, then open the displayed URL and enter its device
code. The TUI starts without a configured model so login and status commands
remain available. Before sending the first prompt, select a Responses-
compatible model either in the environment or from the TUI:

```sh
export TELOS_AGENT_MODEL='your-responses-model'
build/tools/telos
```

The running TUI accepts `/model` to list the catalog and `/model
openai/gpt-5` to select a model without restarting. A model name supplied by
`TELOS_AGENT_MODEL` is registered as a custom model when it is not in the
official catalog.

See [openai-login.md](openai-login.md) for credential storage, refresh,
status, logout, and the API-key alternative.

The following forms are equivalent when starting an interactive session:

```sh
build/tools/telos
build/tools/telos chat
build/tools/telos chat 'start with this request'
```

For scripts or a quick smoke test, run exactly one Turn:

```sh
build/tools/telos run 'reply with a short status'
```

The process exits non-zero when configuration, transport, Provider parsing,
cancellation, or the Agent Turn fails.

## Use a local Responses server

Telos accepts plain HTTP only for the loopback names `localhost`,
`127.0.0.1`, and `[::1]`. Point it at the API prefix; the OpenAI Responses
Provider appends `/responses`:

```sh
export TELOS_AGENT_MODEL='local-model'
export TELOS_AGENT_ENDPOINT='http://127.0.0.1:8080/v1'
build/tools/telos
```

A loopback endpoint receives a non-secret placeholder bearer value when no API
key is set. For other Responses-compatible HTTPS services, set
`TELOS_AGENT_API_KEY` or `OPENAI_API_KEY`. Do not place credentials in
`telos.toml`, command arguments, prompts, logs, or Plugin RPC.

## Configuration

Configuration is resolved in this order:

```text
defaults
-> ~/.telos/config.toml
-> <current-project>/telos.toml
-> environment
-> command line
```

Example `telos.toml`:

```toml
[agent]
provider = "openai-responses"
model = "your-responses-model"
endpoint = "https://api.openai.com/v1"
```

| Configuration | Environment | Command line |
| --- | --- | --- |
| `agent.provider` | `TELOS_AGENT_PROVIDER` | `--provider` |
| `agent.model` | `TELOS_AGENT_MODEL` | `--model` |
| `agent.endpoint` | `TELOS_AGENT_ENDPOINT` | `--endpoint` |

The Project Guidance Plugin loads `~/.telos/AGENTS.md` and each applicable
project `AGENTS.md` into the immutable prompt snapshot. Secrets are resolved
later at the trusted Provider boundary and never enter that snapshot.

The POSIX Tools Plugin exposes the four local tools used by the Agent: `read`,
`write`, `edit`, and `bash`. File paths stay below the current working
directory, file contents are bounded, and shell output is capped. Set
`TELOS_AGENT_DISABLE_BASH=1` when a session must not spawn a shell; set
`TELOS_AGENT_SHELL` to select a compatible shell executable.

## Terminal controls

| Key | Action |
| --- | --- |
| Enter | Submit the editor contents |
| Ctrl+J or Alt+Enter | Insert a new line |
| Left and Right | Move by UTF-8 character |
| Up | Recall the previous prompt when the editor is empty |
| Down | Clear recalled input |
| Backspace or Delete | Remove a character |
| Esc or Ctrl+C | Cancel the active Turn, clear input, or exit when idle |

Bracketed paste is enabled. `/help` displays commands, `/clear` drops the
bounded conversation history, and `/quit` exits. `login` or `/login` starts
OpenAI device login, `/login status` shows the current account state, and
`logout` or `/logout` removes the cached session. When stdin or stdout is not
a terminal, the Frontend automatically uses a plain line-oriented format.

Use `/export PATH` to write the current bounded conversation as JSON and
`/resume PATH` to validate and restore that file. `/fork` and `/clone` keep an
in-process checkpoint for quick branching, while `/resume` without a path
returns to that checkpoint.

## Resource bounds

The terminal Frontend makes one bounded session allocation. Input and recalled
prompts are each limited to 16 KiB. Its worker-to-renderer queue has 64 fixed
entries with 2 KiB text fragments, and the partial output buffer is 8 KiB.
The CLI retains at most 64 conversation messages and 4 MiB of message text;
oldest user/assistant pairs are released when either limit is reached.

The terminal, Authentication, Provider, Transport, and Project Guidance
implementations remain separate Plugins. Zephyr builds do not link the POSIX
terminal, OpenAI OAuth, or libcurl and can provide platform-native Plugins
through the same interfaces.
