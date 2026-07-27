# Telos

Telos is a self-extensible Agentic Framework for Linux and Zephyr. Its core is
a hierarchical state-machine microkernel; providers, tools, policies, stores,
transports, workflow steps, and builders are registered by plugins.

The project is currently in the design phase. The approved architecture is
documented in
[docs/superpowers/specs/2026-07-28-telos-agentic-framework-design.md](docs/superpowers/specs/2026-07-28-telos-agentic-framework-design.md).

Initial platform targets are:

- Linux
- Zephyr `native_sim/native/64`
- Zephyr `qemu_cortex_a53`

Telos uses C17, Meson, and Ninja. Zephyr's upstream CMake build system is used
only through a thin platform adapter for final firmware integration.

## License

Apache-2.0.
