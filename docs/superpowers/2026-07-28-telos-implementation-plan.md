# Telos v0.1 Implementation Plan

## Goal Description

Implement the approved Telos Agentic Framework design as a production-shaped
C17 project that builds with Meson and Ninja on Linux and integrates with
Zephyr through a thin module adapter.

The implementation must include the complete v0.1 non-model surface:

- hierarchical Session state machine and serial Actor event processing
- immutable Events and pluggable Memory, Ring, and Markdown Stores
- transactional Extension Registry and Plugin Generations
- builtin, in-process, process, and static Plugin registration modes
- Plugin Host RPC, lifecycle, health checks, rolling activation, and rollback
- Resource and OpenAI-compatible Skill discovery
- Prompt composition and user/project `AGENTS.md` discovery
- Provider-neutral Items and typed streaming Events
- OpenAI Responses protocol adapter tested against local fixtures
- Tool, Policy, Capability, Secret Reference, and Tool Call orchestration
- Git/local Plugin installation, risk planning, lock verification, build,
  tests, ABI checks, content-addressed cache, staging, and activation
- Linux CLI, Zephyr Shell surface, SDK headers, schemas, and Plugin template
- automated unit, functional, failure-injection, and end-to-end scenario tests
- Linux, `native_sim/native/64`, and `qemu_cortex_a53` verification

Calls to a real remote model API are excluded from automated acceptance. The
Responses adapter, JSON/SSE handling, Tool Call loop, state carry, and failure
behavior remain automated through deterministic local fixtures. A human can
later run the credentialed remote-provider smoke scenario.

The approved design document is the semantic source of truth:

`docs/superpowers/specs/2026-07-28-telos-agentic-framework-design.md`.

## Acceptance Criteria

### AC-1: Reproducible host and Zephyr build surfaces

- Positive tests:
  - A clean `meson setup build` and `meson compile -C build` build all Linux
    libraries, tools, samples, and tests with GCC.
  - The same project builds with Clang.
  - `meson test -C build --print-errorlogs` passes.
  - Zephyr builds the Telos sample for `native_sim/native/64`.
  - Zephyr builds the Telos sample for `qemu_cortex_a53`.
  - The Zephyr adapter contains only module, Kconfig, source attachment, and
    final-link integration.
- Negative tests:
  - Unsupported C standards, ABI versions, and platform configurations fail
    during configuration with actionable diagnostics.
  - Generated source manifests fail verification when stale.

### AC-2: Stable base value, error, ID, time, and cancellation APIs

- Positive tests:
  - Callers create, retain, compare, serialize, and release supported values.
  - Typed errors preserve domain, code, message, and causal error.
  - IDs are unique within a process and parse/format round-trip.
  - Cancellation propagates through a public cancellation token.
- Negative tests:
  - Invalid value types, malformed IDs, overflow, and double completion return
    typed errors without memory corruption.

### AC-3: Hierarchical Session state machine and Actor ordering

- Positive tests:
  - A complete no-tool Turn reaches `COMPLETED`.
  - A Tool Turn follows authorize, execute, collect, and provider continuation.
  - A Session processes Events in sequence while independent Sessions make
    progress concurrently.
  - Multiple authorized Tool Calls execute concurrently and join before the
    next Provider dispatch.
- Negative tests:
  - Illegal transitions, stale Events, direct state mutation attempts, and
    completion after cancellation are rejected.
  - Timeout, retry exhaustion, fatal error, and cancellation reach the
    specified terminal state exactly once.

### AC-4: Immutable Event metadata and pluggable Stores

- Positive tests:
  - Events preserve sequence, IDs, correlation, causation, source, timestamp,
    and payload through every Store.
  - Memory Store returns appended Events in sequence.
  - Ring Store evicts only the oldest Events at capacity.
  - Markdown Store appends parseable, human-readable records and recovers
    records after reopen.
- Negative tests:
  - Duplicate or out-of-order sequence numbers fail.
  - Secret-marked values and full sensitive prompts never appear in Markdown.
  - Partial trailing records are detected without corrupting earlier records.

### AC-5: Transactional Registry, Plugin ABI, lifecycle, and Generations

- Positive tests:
  - A Plugin atomically registers multiple extension descriptors.
  - New Sessions see the latest Registry Generation.
  - Existing Sessions retain their original Generation until release.
  - Lifecycle transitions follow the approved order.
  - A healthy replacement activates while the old version drains.
- Negative tests:
  - Duplicate IDs, missing capabilities, invalid descriptors, incompatible
    ABI, and partial registration leave the active Registry unchanged.
  - An unhealthy replacement never becomes active.

### AC-6: Linux in-process and process Plugin execution

- Positive tests:
  - A trusted shared module initializes through `telos_plugin_init_v1`.
  - A third-party Plugin runs through `telos-plugin-host`.
  - Length-prefixed JSON RPC round-trips requests, Events, errors, and
    cancellation.
  - Plugin Host health checks and graceful shutdown complete.
- Negative tests:
  - Truncated, oversized, malformed, unknown-version, and unauthorized RPC
    frames are rejected.
  - Plugin Host crash, hang, and disconnect fail or retry the owning operation
    without crashing Core.

### AC-7: Resource Generations and OpenAI-compatible Skills

- Positive tests:
  - Resource Manager discovers, validates, snapshots, and reloads Resources.
  - A standard `SKILL.md` with required metadata loads without conversion.
  - Explicit Skill selection and description-based selection load the full
    instructions only after selection.
  - References, assets, scripts, and compatible `agents/openai.yaml` metadata
    are discovered on demand.
  - New Turns see reloaded Resources while active Turns retain old content.
- Negative tests:
  - Missing metadata, invalid frontmatter, path escape, oversized content, and
    unavailable script capabilities return explicit errors.
  - Resource or Skill content cannot grant a Capability.

### AC-8: Prompt Plan and AGENTS.md precedence

- Positive tests:
  - Prompt Composer produces a deterministic immutable Snapshot.
  - Kernel, Policy, user guidance, project guidance, Agent, Workflow, Skill,
    Plugin, and external data appear in the approved order.
  - `~/.telos/AGENTS.md` loads before project guidance.
  - Project `AGENTS.md` files load from project root to current directory.
  - A guidance update changes only new Turn Snapshots.
- Negative tests:
  - A Plugin cannot write into a higher-trust slot.
  - Prompt fragments over budget are deterministically truncated or rejected.
  - Prompt text cannot grant a missing Capability.

### AC-9: Provider-neutral API and OpenAI Responses adapter

- Positive tests:
  - Provider-neutral requests carry instructions, Items, Tools, options, and
    state mode without OpenAI-specific types in Core.
  - Recorded Responses JSON and SSE fixtures map to typed Telos Events.
  - Text, reasoning, usage, function argument deltas, completion, and errors
    preserve order and identifiers.
  - Local state mode emits `store: false` and replays required Items.
  - Remote state mode carries an opaque previous-response reference.
- Negative tests:
  - Unknown SSE Events are handled by the declared compatibility policy.
  - Malformed JSON, invalid UTF-8, broken SSE framing, mismatched Tool Call IDs,
    and transport failure return typed Provider errors.
  - No automated test requires a remote API key.

### AC-10: Tool, Policy, Capability, and Agent loop orchestration

- Positive tests:
  - A registered Tool publishes a schema and executes after authorization.
  - Tool results return as Provider-neutral function outputs.
  - Low-risk allowed actions continue automatically.
  - Multiple Tool Calls join and continue the Agent loop.
- Negative tests:
  - Unknown Tools, invalid arguments, denied capabilities, approval-required
    actions, timeout, cancellation, and Tool failure never report success.
  - Providers cannot invoke local Tools without Core orchestration.

### AC-11: Secret References and trusted injection

- Positive tests:
  - Configuration stores an opaque Secret Reference.
  - A trusted boundary resolves it only for an authorized target.
  - Redacted diagnostics remain useful.
- Negative tests:
  - Secrets never cross normal Plugin RPC, Event, Markdown, CLI JSON, or prompt
    output.
  - Wrong target, missing permission, and untrusted resolver requests fail.

### AC-12: Agent-native Plugin installation

- Positive tests:
  - Local-directory and Git sources complete resolve, quarantine, inspect,
    plan, authorize, dependency verification, build, test, ABI check, stage,
    health check, activate, and commit.
  - A dependency lock round-trips and verifies source hashes.
  - Identical build inputs reuse a content-addressed cache entry.
  - Activation is atomic and a valid prior version can roll back.
  - An ephemeral local Git repository proves Git behavior without a network
    dependency.
- Negative tests:
  - Unknown source, custom build, network, filesystem writes, process spawn,
    or secret use requires the configured approval.
  - Hash mismatch, dirty lock, build failure, test failure, ABI failure,
    health failure, cancellation, and timeout do not activate an artifact.
  - The build environment does not inherit Home, SSH, cloud credentials, or a
    container socket.

### AC-13: SDK, manifest/schema, and Plugin template

- Positive tests:
  - Installed SDK headers and pkg-config/Meson dependency build an external
    sample Plugin.
  - Generated Tool and Provider templates configure, compile, test, and pass
    ABI checks.
  - Manifest and lock schemas accept every shipped example.
- Negative tests:
  - Private Core headers are unavailable to external Plugins.
  - Invalid IDs, unsupported runtime modes, incompatible targets, missing
    permissions, and ABI mismatch fail validation.

### AC-14: CLI, JSON output, configuration, and Zephyr Shell

- Positive tests:
  - Linux commands in the approved design expose help and structured results.
  - Configuration precedence is defaults, user, project, environment, CLI.
  - `plugin`, `resource`, and `state` commands exercise the real public APIs.
  - Zephyr Shell exposes chat, status, Plugins, Resources, and trace commands.
- Negative tests:
  - Invalid commands, invalid config, inaccessible paths, and denied actions
    fail with stable exit codes and typed JSON errors.
  - CLI output never leaks Secrets.

### AC-15: Non-model functional and scenario coverage

- Positive tests:
  - A Linux scenario runs prompt composition, Mock Provider function call,
    Policy authorization, process Tool execution, Event persistence, and final
    completion.
  - A Linux scenario installs a fixture Plugin from Git and uses it in a new
    Session.
  - `native_sim/native/64` runs the same static Tool scenario to completion.
  - `qemu_cortex_a53` boots, runs the same static Tool scenario, emits the
    expected trace, and exits or reports success deterministically.
  - Local transport fixtures exercise network/protocol code without a remote
    model.
- Negative tests:
  - Failure-injection scenarios cover denied Tool, Plugin crash, malformed
    Provider stream, Store failure, install rollback, and cancellation.

### AC-16: Verification quality and traceability

- Positive tests:
  - Every acceptance criterion maps to at least one named automated test,
    except the explicitly manual remote-provider smoke.
  - Every public API has positive and invalid-input coverage.
  - Host coverage reports cover Core and shipped Plugins, with at least 90%
    line coverage and 80% branch coverage.
  - Sanitizer runs are clean.
  - Repeated scenario runs are deterministic.
- Negative tests:
  - CI fails on a missing AC mapping, test skip without an approved reason,
    stale generated file, sanitizer finding, coverage regression below the
    threshold, or Zephyr scenario timeout.

## Path Boundaries

### Upper Bound

The maximum scope is every v0.1 behavior in the approved design, including
both M1 and M2, complete automated non-model verification, public SDK, Linux
runtime, and the two Zephyr targets.

Explicitly excluded:

- public Plugin Registry
- small-MCU resource guarantees
- Zephyr LLEXT
- arbitrary Workflow Graph replacement
- OpenAI-hosted tools
- container or remote Plugin runtime modes
- graphical frontend
- full multi-Agent orchestration
- automatic flashing or OTA
- binary-only Plugin distribution

### Lower Bound

The minimum acceptable release is not a reduced prototype. It must satisfy
AC-1 through AC-16. Remote model calls may remain manual, but their local
protocol and orchestration paths must be automated.

### Allowed Choices

- Use C17, Meson, Ninja, POSIX APIs behind platform boundaries, Zephyr APIs,
  ztest, sanitizers, coverage tooling, QEMU, and small audited dependencies
  that build on both platforms.
- Use mocks only at external boundaries: remote HTTP, time/randomness,
  container runtime, and destructive host operations.
- Prefer real filesystem, process, socket, Git, parser, Registry, Store, and
  state-machine behavior in integration tests.
- Do not introduce CMake as a Telos project-management language. The only
  CMake files are the required thin Zephyr adapter.
- Do not silently skip unavailable required platform tests.
- Do not embed credentials or require a remote model for automated tests.

## Dependencies and Sequence

### Milestone 0: Build and verification spine

1. Add top-level Meson project, options, generated config, public include
   layout, and minimal test harness.
2. Add CI-oriented test suites, sanitizer options, coverage option, generated
   file verification, and AC traceability manifest.
3. Add Zephyr module skeleton, Kconfig, thin CMake adapter, and one booting
   smoke sample.

### Milestone 1: Core event-driven runtime

For each behavior, follow one RED -> GREEN -> refactor cycle:

1. IDs, errors, values, ownership, clock, and cancellation.
2. immutable Events and Event construction validation.
3. hierarchical state transitions and guards.
4. Session Actor queue and terminal behavior.
5. Memory, Ring, and Markdown Stores.
6. deterministic trace output and secret redaction.

### Milestone 2: Extension runtime

1. transactional Registry and descriptor validation.
2. Registry Generations and Session pinning.
3. Plugin ABI and builtin/static registration.
4. in-process Linux loader.
5. versioned RPC framing and Plugin Host process.
6. lifecycle, health check, activation, draining, and crash handling.

### Milestone 3: Resource and prompt plane

1. Resource snapshots and filesystem discovery.
2. OpenAI-compatible Skill metadata and progressive loading.
3. Resource watching and Generations.
4. Prompt Fragment validation and deterministic composition.
5. user and project AGENTS.md discovery and precedence.

### Milestone 4: Provider, Tool, and Policy loop

1. Provider-neutral Items and Event API.
2. deterministic Mock Provider tracer bullet.
3. Tool schema, Registry lookup, Policy decision, execution, and continuation.
4. parallel Tool join, cancellation, timeout, and failures.
5. Responses JSON/SSE adapter against fixtures.
6. Secret Reference and trusted transport injection.

### Milestone 5: SDK and self-installation

1. Plugin manifest and lock parser/validator.
2. SDK install metadata and external template contract.
3. local and Git source resolution into quarantine.
4. risk inspection and approval plan.
5. Builder Backend and isolated command execution.
6. tests, ABI check, content-addressed cache, staging, health, activation, and
   rollback.

### Milestone 6: Product surfaces and complete scenarios

1. Linux CLI and JSON output.
2. Zephyr Shell commands.
3. Linux process-Plugin Agent scenario.
4. Linux Git-install-and-use scenario.
5. native simulator static-Plugin scenario.
6. QEMU Cortex-A53 static-Plugin and local transport scenario.
7. failure-injection matrix, sanitizer suite, coverage gate, and traceability
   audit.

## TDD Execution Rules

1. Implement vertical behavior slices, never all tests before all code.
2. Write one failing test through a public interface.
3. Confirm the failure is caused by missing behavior.
4. Implement only enough production code to pass.
5. Run the focused test, then the affected suite.
6. Refactor only while green.
7. Run host, sanitizer, and relevant Zephyr checks before committing a
   milestone.
8. Keep test names in Telos domain vocabulary from the approved design.
9. Do not put AC numbers or plan terminology in production code.

## Feasibility Notes

- The existing machine has Meson, Ninja, GCC, Clang, west, QEMU, and an
  existing Zephyr v4.4.0 west workspace at
  `/home/zevorn/rt-claw/vendor/os`.
- `native_sim/native/64` can build with the host toolchain. Cortex-A53 builds
  may require correcting or installing the configured Zephyr SDK before the
  final target verification.
- Network-facing behavior should first use a local deterministic fixture
  server or recorded byte streams. This exercises parsing and state flow
  without user credentials.
- A small cross-platform test support header may map assertions to the host
  harness and ztest while keeping production interfaces identical.
- Content-addressed caches and build sandboxes should use injected filesystem,
  clock, process, and container-runtime boundaries so failure behavior is
  deterministic.

## Completion Evidence

Completion requires all of the following current-state evidence:

- clean Git worktree on the implementation commit
- GCC and Clang builds
- complete Meson test log
- sanitizer test log
- coverage report meeting thresholds
- SDK external-Plugin build log
- Linux functional and scenario logs
- `native_sim/native/64` build and scenario output
- `qemu_cortex_a53` build and scenario output
- AC-to-test traceability report with no uncovered criterion
- documented manual command for the later credentialed Responses smoke test

No subset of these artifacts proves the complete objective.
