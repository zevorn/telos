# Telos

Telos is a self-extensible Agentic Framework for Linux, macOS, and Zephyr. Its
core is a hierarchical state-machine microkernel; providers, tools, policies,
stores, transports, workflow steps, and builders are registered by plugins.

Telos v0.1 provides:

- a serial Session Actor and hierarchical state machine
- Memory, Ring, and append-only Markdown Event Store Plugins
- transactional Plugin registration, rolling Generations, and a process host
- OpenAI-compatible Agent Skills and deterministic prompt composition
- Provider-neutral Items and an OpenAI Responses Provider Plugin
- a Pi-style POSIX terminal Frontend and cancellable curl Transport Plugins
- bounded POSIX `read`, `write`, `edit`, and `bash` Tool Plugins
- Policy-controlled Tools, Capability and Secret Brokers, and an Agent loop
- source-first Plugin inspection, build, test, cache, activation, and rollback
- a public Meson Plugin SDK and host CLI
- thin Linux and Zephyr platform adapters
- static Zephyr integration for `native_sim/native/64` and ARM Virt

## Build and test

Telos requires a C17 compiler, Meson 1.4 or newer, Ninja, and pkg-config. An
interactive Linux or macOS host build also requires libcurl development
headers.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

All test cases are organized below `tests/` into unit, Plugin, and functional
suites. Together they cover invalid input, failure injection, ABI/SDK
contracts, and complete non-model Agent scenarios. See
[docs/testing.md](docs/testing.md) for GCC, Clang, sanitizer, coverage, and
Zephyr commands.

Telos uses C17, Meson, and Ninja. Zephyr's upstream CMake build system is used
only through a thin platform adapter for final firmware integration.

## Start the terminal Agent

Install the libcurl development package, then require the host Transport at
configuration time:

```sh
meson setup build -Dcurl_transport=enabled
meson compile -C build

export TELOS_AGENT_MODEL='your-responses-model'
build/tools/telos
```

You can also start without a model, run `/login` (or `/login PROVIDER`), and
select one from the catalog with `/model` while the TUI is running. Use
Up/Down and Enter in the selector, or type `/model PROVIDER/MODEL` directly.
While typing a slash command, matching commands appear above the editor; Tab
cycles through them.
The selected model and thinking level are saved as user defaults. `/thinking
LEVEL` sets `off`, `minimal`, `low`, `medium`, `high`, `xhigh`, or `max` for
reasoning-capable models; `/setting` shows settings and accepts
`model MODEL`, `thinking LEVEL`, or `status FIELDS`. `/status` and
`/setting status` configure the footer fields (`model`, `thinking`, `path`,
`branch`, and `context`; use `all` or `none` as shortcuts).
The latter also registers a bounded custom model when the identifier is not in
the catalog.

The interactive footer shows estimated context usage and the model window.
Tool calls are grouped in a compact, scrolling panel above the editor; Ctrl+O
expands or collapses the panel. Agent responses render common Markdown such as
headings, lists, emphasis, links, and fenced code blocks.

Use `/export PATH` to write the current bounded conversation as JSON. Use
`/sessions` followed by `/resume SESSION` to switch to a saved JSONL session;
in the TUI, `/resume` followed by Tab lists named sessions above the editor and
Enter switches to the selected one. Session names are inferred from the first
user request, persisted with the JSONL record, and can be changed with
`/rename NAME` (or `/name NAME`). `/resume PATH` also validates and restores an
exported file. `/fork` and `/clone` keep an in-process checkpoint for quick
branching, while `/resume` without a path returns to that checkpoint.

Running `telos` without a command opens the interactive terminal UI. Use
`telos chat`, or run a single pipeline-friendly turn with
`telos run 'your prompt'`. Responses-compatible local servers can be selected
with `TELOS_AGENT_ENDPOINT=http://127.0.0.1:PORT/v1`; loopback HTTP does not
require a real API key.

For automation, add `--json` to emit one bounded JSON event per line:

```sh
build/tools/telos --json run 'your prompt'
```

Use `--continue` with `chat` to load the newest bounded JSONL session from
`~/.telos/sessions` before accepting new input.

Process integrations can use strict JSONL RPC mode. Send a prompt request as
`{"type":"prompt","message":"..."}` and terminate with
`{"type":"quit"}`:

```sh
printf '%s\n' '{"type":"prompt","message":"hello"}' \
    '{"type":"quit"}' | build/tools/telos --mode rpc chat
```

Inside the TUI, type `login` or `/login` to authenticate with an OpenAI
ChatGPT account. `/login deepseek`, `/login zai`, and `/login anthropic` use
the corresponding API-key Plugins; their environment variables are
`DEEPSEEK_API_KEY`, `ZAI_API_KEY`, and `ANTHROPIC_API_KEY`. `/login-status
PROVIDER` and `/logout PROVIDER` address a provider without changing the
active model. `/thinking LEVEL` selects `off`, `minimal`, `low`, `medium`,
`high`, `xhigh`, or `max` when the selected model advertises reasoning. Use
`/setting` to inspect or change the common model and thinking defaults.
API-key authentication remains available through `OPENAI_API_KEY`. See [the
OpenAI login guide](docs/openai-login.md) for the device-code flow,
credential storage, status, and logout commands.

See [the terminal Agent guide](docs/linux-agent.md) for configuration,
key bindings, local endpoints, and troubleshooting.

## Documentation

- [Telos documentation](docs/README.md)
- [memory and resource model](docs/memory.md)
- [official Plugins and contribution guide](plugins/README.md)
- [verification guide](docs/testing.md)
- [terminal Agent on Linux and macOS](docs/linux-agent.md)
- [OpenAI login from the terminal](docs/openai-login.md)

Replaceable behavior belongs in `plugins/`; Core is limited to shared runtime
invariants and extension interfaces. Contributions of focused, well-tested
Plugins are welcome.

## License

Apache-2.0.
