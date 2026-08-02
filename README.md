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

Running `telos` without a command opens the interactive terminal UI. Use
`telos chat`, or run a single pipeline-friendly turn with
`telos run 'your prompt'`. Responses-compatible local servers can be selected
with `TELOS_AGENT_ENDPOINT=http://127.0.0.1:PORT/v1`; loopback HTTP does not
require a real API key.

Inside the TUI, type `login` or `/login` to authenticate with an OpenAI
ChatGPT account. API-key authentication remains available through
`OPENAI_API_KEY`. See [the OpenAI login guide](docs/openai-login.md) for the
device-code flow, credential storage, status, and logout commands.

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
