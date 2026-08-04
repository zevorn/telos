# Telos documentation

Telos is a self-extensible agent runtime for Linux, macOS, and Zephyr. The
runtime owns execution invariants; concrete providers, stores, tools, policies,
and other replaceable behavior belong in Plugins.

This document describes the repository as it is built today. Verification
commands and platform-specific test details live in [testing.md](testing.md),
and the upstream Plugin catalog and contribution rules live in
[`plugins/README.md`](../plugins/README.md).

## Build Telos

Telos requires a C17 compiler, Meson 1.4 or newer, Ninja, and pkg-config. An
interactive Linux or macOS host build also requires libcurl development
headers.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Useful host commands after a build include:

```sh
build/tools/telos --help
build/tools/telos doctor
build/tools/telos plugin list
```

On Linux and macOS, a build with libcurl also provides the interactive
terminal Agent:

```sh
TELOS_AGENT_MODEL='your-responses-model' \
build/tools/telos
```

Type `login` inside the TUI to authenticate with OpenAI. See
[openai-login.md](openai-login.md) for the device-code flow and
[linux-agent.md](linux-agent.md) for the complete terminal and local endpoint
workflow.

The default build contains the Core and all official Plugins in this
repository. Applications embedding Telos through Meson can use
`telos_core_dep` when they need only Core mechanisms, or `telos_dep` for Core
plus the official Plugin set.

## Architecture

```text
Applications and frontends
            |
      Agent runtime
            |
      Session actors
            |
 Hierarchical state machine
            |
 Registry + capability broker
       +----+----+
       |         |
  Resources    Plugins
       |         |
 Platform integration
            |
 Linux / macOS / Zephyr
```

### Core

Core contains mechanisms whose invariants must be shared by every extension:

- immutable Values, Events, IDs, Errors, clocks, and cancellation;
- the serial Reactor loop, Session Actors, and hierarchical state transitions;
- transactional extension registration and immutable Registry Generations;
- Plugin lifecycle, loading, process RPC, and capability enforcement;
- provider-neutral request, Event, Tool, and Event Store interfaces;
- Resource and prompt snapshot validation;
- secret references and trusted resolution;
- source inspection, build, staging, activation, and rollback orchestration.

Core does not own a model protocol or a concrete Event Store. Those
implementations are official Plugins under `plugins/` and may be replaced
without changing the Core interfaces.

This boundary is intentionally strict: Core owns state-machine and Reactor
correctness, lifecycle, and the contracts needed to host Plugins. A feature
that can vary by deployment belongs behind one of those contracts in
`plugins/`, even when Telos ships an official implementation.

### Official Plugins

The repository currently ships these Plugins:

| Plugin | Kind | Purpose |
| --- | --- | --- |
| `dev.zevorn.openai-codex-auth` | Authentication | Provides OpenAI device login, token refresh, and a private host credential cache. |
| `dev.zevorn.openai-responses` | Provider | Maps provider-neutral requests and streaming Events to the OpenAI Responses protocol. |
| `dev.zevorn.memory-store` | Store | Keeps an unbounded Event sequence in memory. |
| `dev.zevorn.ring-store` | Store | Keeps a fixed-capacity Event window. |
| `dev.zevorn.markdown-store` | Store | Persists append-only, human-readable Event records. |
| `dev.zevorn.agent-skills` | Context Source | Discovers and validates filesystem-backed Agent Skills on POSIX hosts. |
| `dev.zevorn.project-guidance` | Context Source | Loads ordered user and project guidance on POSIX hosts. |
| `dev.zevorn.posix-tools` | Tool | Provides bounded `read`, `write`, `edit`, and `bash` tools on POSIX hosts. |
| `dev.zevorn.tui-frontend` | Frontend | Provides bounded main-screen terminal interaction on Linux and macOS. |
| `dev.zevorn.curl-transport` | Transport | Streams cancellable HTTP responses through libcurl on Linux and macOS. |

Official Plugins are built and tested with the repository. When shared Plugin
support is enabled, Meson also produces loadable modules with the versioned
`telos_plugin_init_v1` entry point. Built-in and static callers can use each
Plugin's header below `telos/plugins/`.

See the [Plugin guide](../plugins/README.md) for package layout, contribution
requirements, and the rule used to decide whether new behavior belongs in
Core or a Plugin.

See [memory.md](memory.md) for the bounded-allocation rules shared by Core and
Plugins, including the zero-allocation utility headers available to both
Linux and Zephyr builds.

## Sessions and Events

Each Session is an Actor with a private Event queue. A Session processes one
Event at a time; independent Sessions may run concurrently. Work may happen in
threads, processes, or transports, but completion re-enters the Session as an
Event.

A normal Turn follows this state flow:

```text
IDLE
  -> TURN_ACCEPTED
  -> INPUT_PREPARE
  -> CONTEXT_BUILD
  -> PROVIDER_DISPATCH
  -> RESPONSE_PROCESS
       +-> TOOL_AUTHORIZE
       -> TOOL_EXECUTE
       -> TOOL_COLLECT
       -> CONTEXT_BUILD
       -> PROVIDER_DISPATCH
       +-> FINAL_COMMIT
  -> COMPLETED
```

Cancellation, timeout, retry exhaustion, and fatal errors enter explicit
terminal paths. Plugins cannot select arbitrary Core states; a State Fragment
attaches only at a declared extension slot and returns a constrained outcome.

Every persisted Event carries:

- a monotonic sequence number;
- Event, Session, correlation, and causation IDs;
- a type and source;
- a timestamp;
- an immutable payload.

Sensitive Values are redacted before trace or Markdown serialization.

## Registry Generations

A Plugin registers one or more extension descriptors inside a transaction.
Core validates the complete transaction before publishing it. Registration is
all-or-nothing: an invalid descriptor or unavailable capability leaves the
active Registry unchanged.

New Sessions acquire the latest immutable Registry Generation. Existing
Sessions keep their prior Generation until they finish, which permits rolling
activation without changing behavior in the middle of a Turn.

Supported extension kinds are:

- Provider;
- Tool;
- Policy;
- Context Source;
- Store;
- Workflow Step;
- Transport;
- Codec;
- Frontend;
- Builder;
- State Fragment;
- Event Handler;
- Prompt provider.

## Provider and Tool flow

Core works with provider-neutral requests and typed streaming Events. A
Provider Plugin translates those values to and from a remote model protocol.
The OpenAI-specific request builder and SSE parser therefore live in
`plugins/providers/openai-responses/`, not in `src/`.

Tool Calls always return through Core orchestration:

```text
Provider function call
-> Tool schema validation
-> lookup in the pinned Registry Generation
-> capability and Policy decision
-> Tool Plugin execution
-> Tool result
-> provider-neutral function output
-> next Provider dispatch
```

A Provider cannot invoke a local Tool directly. Multiple authorized Tool
Calls may run concurrently, but Core joins their results before continuing the
Agent loop.

Provider state can be local or remote. Local mode sends `store: false` and
replays the required Items from Telos state. Remote mode carries an opaque
previous-response identifier. The local Event Store remains the audit source
in both modes.

## Event Stores

The Core Event Store interface exposes append, count, indexed read, and
destruction. Concrete adapters live in `plugins/stores/`:

- Memory Store is useful for short-lived Sessions and tests.
- Ring Store bounds memory use by evicting the oldest Event at capacity.
- Markdown Store appends recoverable records to a filesystem path.

A Markdown record contains readable metadata and a structured payload:

````md
## 42 · tool.completed

- Event: `evt_...`
- Correlation: `turn_...`
- Source: `plugin:dev.example.echo`

```json
{"tool":"echo","status":"ok","output":"hello"}
```
````

Plugin authors implement the Event Store seam through
`<telos/store_plugin.h>`. Applications should use the generic functions in
`<telos/store.h>` after construction rather than depend on an adapter's
implementation.

## Resources and prompts

Resources are data rather than native modules. Current Resource kinds include
Skills, prompts, workflows, templates, themes, knowledge, declarative policy,
and configuration.

Telos reads OpenAI-compatible Agent Skills without private metadata:

```text
skill-name/
├── SKILL.md
├── scripts/
├── references/
├── assets/
└── agents/
    └── openai.yaml
```

The Linux Agent Skills Plugin discovers Skills from project and user Resource
roots. Instructions are loaded progressively after explicit selection or
description matching. Skill content cannot grant a capability that Core
Policy did not provide.

The System Prompt is an immutable snapshot assembled in this order:

```text
KERNEL CONTRACT
> ENFORCED POLICY
> USER GUIDANCE
> PROJECT GUIDANCE
> AGENT DEFINITION
> WORKFLOW
> SELECTED SKILL
> PLUGIN INSTRUCTIONS
> EXTERNAL DATA
```

User guidance comes from `~/.telos/AGENTS.md`. Project guidance is discovered
from the project root through the current directory; later, more-specific
files win within the project-guidance layer. Changes affect new Turn snapshots
only.

The Kernel Contract is compact continuous text rather than presentation
Markdown. It tells an Agent to use only tools visible in its pinned Registry
Generation, inspect relevant context, communicate concise progress, preserve
user state, verify outcomes, and report completed, unverified, or blocked work
accurately. Available Tool descriptions and changing runtime context belong in
the ordered fragments; they are not hard-coded into the contract.

This separation follows the useful shape of
[Pi's system prompt builder](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/src/core/system-prompt.ts):
keep the stable operating contract concise, and inject the tools and context
that are actually available at runtime. The Telos wording and trust layers are
its own and reflect the Registry, Policy, and lifecycle model described here.

## Plugin lifecycle and runtime modes

The lifecycle is:

```text
DISCOVERED -> VERIFIED -> LOADED -> INITIALIZING
-> ACTIVE -> QUIESCING -> STOPPED -> UNLOADED
```

Telos supports these v0.1 runtime modes:

- `builtin`: trusted Plugin implementation linked into the host distribution;
- `inprocess`: explicitly trusted Linux shared module;
- `process`: isolated Linux Plugin Host using length-prefixed JSON RPC;
- `static`: firmware-linked Plugin initialized through the same ABI contract.

Third-party Linux Plugins should use process mode unless Policy explicitly
trusts them in-process. A crashing process Plugin can fail or retry its owning
operation, but cannot crash Core.

## Configuration

Configuration precedence, from lowest to highest, is:

```text
defaults
-> ~/.telos/config.toml
-> <project>/telos.toml
-> TELOS_* environment variables
-> command-line overrides
```

Provider credentials are represented by opaque secret references. Do not put
secret values in configuration files, command arguments, prompts, Event
payloads, or normal Plugin RPC. A trusted broker resolves a reference only for
an authorized target.

Core configuration contains only runtime selections such as the active
Provider ID, state directory, and builder backend. Provider-specific keys,
defaults, environment variables, and validation belong to the Provider Plugin
that consumes them.

The host terminal composition currently reads `agent.provider`,
`agent.model`, and `agent.endpoint`. Their environment equivalents are
`TELOS_AGENT_PROVIDER`, `TELOS_AGENT_MODEL`, and `TELOS_AGENT_ENDPOINT`;
`--provider`, `--model`, and `--endpoint` provide one-process overrides.

## Repository layout

```text
telos/
├── include/telos/       Core public interfaces
├── src/                 Core implementations
├── plugins/             Official and contributed Plugins
│   ├── authentication/
│   ├── context-sources/
│   ├── providers/
│   ├── resources/
│   ├── stores/
│   ├── transports/
│   └── frontends/
├── sdk/                 schemas and Plugin templates
├── scripts/             source-style checks
├── tools/               CLI, builder, host, and ABI checker
├── platforms/linux/     thin Linux host integration
├── platforms/zephyr/    thin Zephyr integration
├── samples/             runnable examples
├── tests/               unit, Plugin, functional, and acceptance tests
└── docs/                user and maintainer documentation
```

New replaceable behavior starts in `plugins/`. A Core change is justified only
when the shared interface or a global invariant must change to support that
behavior.

## Host systems and Zephyr

Linux uses the top-level Meson build and the thin adapter under
`platforms/linux/`. The adapter exposes the canonical Plugin target and host
resource facts through caller-owned storage as `telos_linux_platform_dep`;
`telos_dep` includes it on Linux. Zephyr retains its upstream CMake build only
as a thin integration layer for Kconfig, Devicetree, board selection, source
attachment, and final firmware linking.

macOS uses the same top-level Meson host build. It compiles the portable Core,
Plugin SDK, CLI, process host, and POSIX terminal and curl Plugins without
adding platform behavior to Core. The Linux adapter remains Linux-specific.

Supported verification targets are:

```sh
west build -b native_sim/native/64 samples/telos_agent
west build -b qemu_cortex_a53 samples/telos_agent
```

See [testing.md](testing.md) for the exact west workspace commands, QEMU
runners, sanitizer setup, coverage gate, acceptance traceability, and the
manual credentialed Provider smoke test.
