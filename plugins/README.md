# Telos Plugins

This directory is the upstream home for official Telos Plugins. Community
Plugins are welcome here: once accepted, they are built, tested, reviewed, and
released under the same rules as the Plugins maintained by the core team.

## Catalog

| Plugin | Kind | Platforms |
| --- | --- | --- |
| [OpenAI Codex Authentication](authentication/openai-codex/README.md) | Authentication | Linux and macOS |
| [Provider API-key Authentication](authentication/api-key/README.md) | Authentication | Linux and macOS |
| [POSIX Agent Tools](tools/posix/README.md) | Tool | Linux and macOS |
| [OpenAI Responses](providers/openai-responses/README.md) | Provider | Linux, macOS, and Zephyr |
| [Memory Store](stores/memory/README.md) | Store | Linux, macOS, and Zephyr |
| [Ring Store](stores/ring/README.md) | Store | Linux, macOS, and Zephyr |
| [Markdown Store](stores/markdown/README.md) | Store | Linux, macOS, and Zephyr with a filesystem |
| [JSONL Store](stores/jsonl/README.md) | Store | Linux and macOS with a filesystem |
| [Agent Skills](resources/agent-skills/README.md) | Context Source | Linux and macOS |
| [Project Guidance](context-sources/project-guidance/README.md) | Context Source | Linux and macOS |
| [Terminal Frontend](frontends/tui/README.md) | Frontend | Linux and macOS |
| [curl Transport](transports/curl/README.md) | Transport | Linux and macOS |
| [Git Branch Status](tui/git-branch-status/README.md) | TUI Plugin | Linux and macOS |

## Plugin-first rule

Put behavior in a Plugin when it can vary independently of the runtime. This
includes model protocols, Tools, Policies, concrete Stores, Transports,
codecs, frontends, workflow steps, builders, context sources, and prompt
providers.

Core is reserved for shared invariants and the interfaces at extension seams:

- Session ordering and state-machine safety;
- immutable Event and Value semantics;
- Registry Generations and Plugin lifecycle;
- capability, Policy, cancellation, timeout, and secret enforcement;
- versioned host and process protocols;
- provider-neutral and Store-neutral contracts.

If a Plugin cannot be implemented through an existing interface, first decide
whether the missing concept is shared by at least two adapters. When it is,
deepen the Core interface at that seam and keep the concrete behavior here.
Do not add a provider, Tool, Store, or platform special case to Core merely to
avoid defining the right Plugin contract.

## Repository layout

Plugins are grouped by extension kind:

```text
plugins/
├── authentication/
├── context-sources/
├── frontends/
├── providers/
├── resources/
├── stores/
├── tools/
├── transports/
├── tui/
└── builders/
```

Only categories with an upstream Plugin need to exist in Git. Add a category
when its first Plugin is contributed.

A Plugin package has this shape:

```text
plugin-name/
├── plugin.toml
├── telos.lock
├── meson.build
├── README.md
├── LICENSE
├── include/             optional public Plugin interface
├── src/
└── resources/           optional Skills, prompts, or other data
```

The repository-wide Apache-2.0 license covers in-tree Plugins, so a separate
`LICENSE` may be omitted only when the Plugin uses that same license.
Focused test cases for every in-tree Plugin live in the repository-wide
[`tests/plugins/`](../tests/plugins/) suite. Keeping test code there gives the
whole project one test entry without making test fixtures part of a Plugin's
source lock.

## Manifest and identity

Every Plugin must declare:

- a globally unique reverse-domain ID;
- a human-readable name and version;
- Plugin ABI version `1` and entry point `telos_plugin_init_v1`;
- supported runtime modes and targets;
- Meson as its build system;
- the minimum required capabilities.

Request narrowly scoped capabilities. A package that provides several
independently useful features should normally be split when doing so lets
users grant fewer permissions.

`telos.lock` must describe the committed source and every external dependency.
Do not use an all-zero source hash in a contribution. Regenerate the hash
whenever files in the Plugin package change.

## Implementation contract

Plugin initialization is transactional:

```c
int telos_plugin_init_v1(const struct telos_host_api_v1 *host,
                         struct telos_plugin_registrar_v1 *registrar);
```

Validate both ABI versions and required function pointers before registering
anything. Submit all descriptors through the registrar. Returning a non-zero
value aborts the whole transaction.

Plugins must:

- use Host interfaces instead of reaching into private `src/` headers;
- treat normal Plugin output as untrusted data;
- return typed errors and honor cancellation;
- keep secrets out of Events, logs, prompts, and ordinary RPC fields;
- declare every capability before attempting the operation;
- state ownership and resource bounds explicitly, and provide caller-owned or
  static storage when the Plugin targets Zephyr;
- avoid hidden network access or dependency downloads during a build;
- preserve existing Sessions when a new Registry Generation activates.

Headers below `include/telos/` define Core interfaces. Headers below a Plugin's
`include/telos/plugins/` directory belong to that Plugin. The default
distribution aggregates official Plugin dependencies as `telos_dep`, while
`telos_core_dep` remains free of concrete provider and Store implementations.

## Build and test

The top-level build compiles all official Plugins:

```sh
meson setup build
meson compile -C build
meson test -C build --suite plugins --print-errorlogs
```

With `-Dshared_plugins=true`, the build also produces loadable modules and the
`official-plugins` test loads every module through the public Plugin ABI. Each
Plugin must also have focused tests through its public interface, including
invalid configuration, denied capability, cancellation, and failure behavior
where applicable.

Before opening a contribution, run the GCC and Clang suites described in
[`docs/testing.md`](../docs/testing.md). Changes that affect Zephyr-supported
Plugins must also pass the relevant native simulator and ARM QEMU gates.

## Contributing a Plugin

1. Choose the narrowest extension kind and create the package under its
   category.
2. Start from the closest template in [`sdk/templates/`](../sdk/templates/).
3. Add a complete manifest, dependency lock, README, and implementation, plus
   public-interface tests below [`tests/plugins/`](../tests/plugins/).
4. Integrate the package in the category's `meson.build`; add it only to
   `telos_official_plugins_dep`, never to `telos_core_dep`.
5. Document configuration, capabilities, supported targets, runtime modes,
   observable failure behavior, and any manual verification.
6. Run the full host suite and every claimed platform test.

Pull requests should be small enough to review as one Plugin. A contribution
that also needs a Core interface change should separate the shared seam from
the adapter implementation whenever practical.
