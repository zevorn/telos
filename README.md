# Telos

Telos is a self-extensible Agentic Framework for Linux and Zephyr. Its core is
a hierarchical state-machine microkernel; providers, tools, policies, stores,
transports, workflow steps, and builders are registered by plugins.

Telos v0.1 provides:

- a serial Session Actor and hierarchical state machine
- Memory, Ring, and append-only Markdown Event Store Plugins
- transactional Plugin registration, rolling Generations, and a process host
- OpenAI-compatible Agent Skills and deterministic prompt composition
- Provider-neutral Items and an OpenAI Responses Provider Plugin
- Policy-controlled Tools, Capability and Secret Brokers, and an Agent loop
- source-first Plugin inspection, build, test, cache, activation, and rollback
- a public Meson Plugin SDK and Linux CLI
- thin Linux and Zephyr platform adapters
- static Zephyr integration for `native_sim/native/64` and ARM Virt

## Build and test

Telos requires a C17 compiler, Meson 1.4 or newer, Ninja, and pkg-config.

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

## Documentation

- [Telos documentation](docs/README.md)
- [memory and resource model](docs/memory.md)
- [official Plugins and contribution guide](plugins/README.md)
- [verification guide](docs/testing.md)

Replaceable behavior belongs in `plugins/`; Core is limited to shared runtime
invariants and extension interfaces. Contributions of focused, well-tested
Plugins are welcome.

## License

Apache-2.0.
