# Telos

Telos is a self-extensible Agentic Framework for Linux and Zephyr. Its core is
a hierarchical state-machine microkernel; providers, tools, policies, stores,
transports, workflow steps, and builders are registered by plugins.

Telos v0.1 provides:

- a serial Session Actor and hierarchical state machine
- Memory, Ring, and append-only Markdown Event Stores
- transactional Plugin registration, rolling Generations, and a process host
- OpenAI-compatible Agent Skills and deterministic prompt composition
- Provider-neutral Items and an OpenAI Responses protocol adapter
- Policy-controlled Tools, Capability and Secret Brokers, and an Agent loop
- source-first Plugin inspection, build, test, cache, activation, and rollback
- a public Meson Plugin SDK and Linux CLI
- static Zephyr integration for `native_sim/native/64` and ARM Virt

## Build and test

Telos requires a C17 compiler, Meson 1.4 or newer, Ninja, and pkg-config.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

The verification suite includes unit, invalid-input, functional, failure
injection, ABI/SDK contract, and complete non-model Agent scenarios. See
[docs/testing.md](docs/testing.md) for GCC, Clang, sanitizer, coverage, and
Zephyr commands.

Telos uses C17, Meson, and Ninja. Zephyr's upstream CMake build system is used
only through a thin platform adapter for final firmware integration.

The approved architecture and acceptance criteria are documented in:

- [Agentic Framework design](docs/superpowers/specs/2026-07-28-telos-agentic-framework-design.md)
- [implementation and acceptance plan](docs/superpowers/2026-07-28-telos-implementation-plan.md)

## License

Apache-2.0.
