# Telos verification

Telos keeps remote model credentials out of automated tests. Everything below
the credentialed HTTPS boundary is exercised with deterministic fixtures:
request construction, streaming Responses parsing, local and remote state,
Tool Calls, Policy decisions, process Plugins, persistence, and failure paths.

## Source style

Run the QEMU-derived source checks plus the Telos function-layout rule on
changed C files:

```sh
scripts/check-style.sh
```

Use `scripts/check-style.sh --all` when changing the style baseline. Function
declarations and definitions keep their first parameter beside the opening
parenthesis; additional parameters wrap at 80 columns and align with the first
parameter. See [`scripts/README.md`](../scripts/README.md) for provenance and
details.

## Test layout

`tests/` is the only test-case tree and `tests/meson.build` is the only Meson
test registration entry. Tests are assigned to exactly one suite:

- `unit` covers focused Core and host platform behavior;
- `plugins` covers the Plugin ABI, lifecycle, loading, installation, and every
  official Plugin implementation;
- `functional` covers CLI and build contracts plus complete Linux and Zephyr
  scenarios.

Run all suites with the normal `meson test` command, or select one explicitly:

```sh
meson test -C build --suite unit --print-errorlogs
meson test -C build --suite plugins --print-errorlogs
meson test -C build --suite functional --print-errorlogs
```

See [`tests/README.md`](../tests/README.md) for placement rules. The acceptance
checker rejects tests that do not belong to exactly one of these suites.

## Linux

Run a clean GCC build:

```sh
CC=gcc meson setup --wipe build-gcc
meson compile -C build-gcc
meson test -C build-gcc --print-errorlogs
python3 tests/check_acceptance.py \
    tests/acceptance.toml \
    build-gcc \
    --test-log build-gcc/meson-logs/testlog.json
```

Run the same suite with Clang:

```sh
CC=clang meson setup --wipe build-clang
meson compile -C build-clang
meson test -C build-clang --print-errorlogs
```

Run AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
CC=clang meson setup --wipe build-sanitize \
    -Db_sanitize=address,undefined \
    -Db_lundef=false
meson compile -C build-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
meson test -C build-sanitize --print-errorlogs
```

Run the coverage gate:

```sh
python3 -m pip install gcovr==8.6
tests/check_coverage.sh build-coverage
```

The report covers production files below `src/`, `plugins/`, and
`platforms/linux/`. Tests are outside those filters. The gate requires at
least 90% line coverage and 80% branch coverage. The
`allocation-failure-matrix` test injects failures into every observed
`malloc`, `calloc`, and `realloc` call for the public subsystems it exercises.

When libcurl is available, `terminal-frontend`, `terminal-tui`,
`curl-transport`, `agent-chat`, and `agent-chat-oauth` cover plain pipes, a
real pseudoterminal, cancellation validation, OpenAI device login, credential
headers, and end-to-end local streaming Responses requests. The OAuth fixture
uses only loopback endpoints and synthetic credentials; no model credential is
used by these tests.

## macOS development host

Use Apple Clang and ensure pkg-config can resolve libcurl, then run the same
unit, Plugin, and functional suites:

```sh
pkg-config --modversion libcurl
meson setup build -Dcurl_transport=enabled
meson compile -C build
meson test -C build --print-errorlogs
```

For an existing build directory, replace `meson setup build` with
`meson setup --reconfigure build`. The allocation-failure test uses Darwin's
malloc zones because Apple ld does not implement GNU ld's `--wrap` option.

## Zephyr

The repository contains a west manifest pinned to Zephyr 4.4.0. Zephyr 4.4
sets C17 as its minimum C language version; its older C99 and C11 compatibility
options are deprecated. Telos therefore uses one C17 baseline on Linux and
Zephyr instead of maintaining a lower C dialect. See Zephyr's official
[4.4 migration guide](https://docs.zephyrproject.org/latest/releases/migration-guide-4.4.html).

From a west workspace whose self project is `telos`, build and run the native
simulator:

```sh
west build -p always \
    -d build-zephyr-native \
    -b native_sim/native/64 \
    telos/samples/telos_agent \
    -- -DZEPHYR_EXTRA_MODULES="$PWD/telos/platforms/zephyr"
telos/tests/functional/run_zephyr_native.sh build-zephyr-native
```

Build and run the ARM Virt scenario:

```sh
west build -p always \
    -d build-zephyr-qemu \
    -b qemu_cortex_a53 \
    telos/samples/telos_agent \
    -- -DZEPHYR_EXTRA_MODULES="$PWD/telos/platforms/zephyr"
telos/tests/functional/run_zephyr_qemu.sh build-zephyr-qemu
```

The QEMU runner attaches the emulated E1000 device to QEMU user networking.
It does not require a TAP device or host network administration privileges.
Both runners require the boot banner, scenario success marker, and completed
trace before accepting the run. The ARM runner additionally requires the
Zephyr socket layer to send a DNS query through QEMU user networking, receive
a valid response, and report its configured IPv4 address.

## Acceptance traceability

`tests/acceptance.toml` maps AC-1 through AC-16 to configured Meson test names
and external platform gates. The checker fails if a criterion is missing, a
mapped test is not configured, the manual model smoke is not declared, or a
test was skipped without an approved reason.

| Criterion | Verified surface |
| --- | --- |
| AC-1 | Reproducible host and Zephyr build contracts |
| AC-2 | Values, Errors, IDs, clocks, and cancellation |
| AC-3 | Session state transitions and Actor ordering |
| AC-4 | Immutable Event metadata and Event Stores |
| AC-5 | Registry transactions, Plugin lifecycle, and Generations |
| AC-6 | in-process and process Plugin execution |
| AC-7 | Resource Generations and compatible Skills |
| AC-8 | prompt composition, trust, and guidance precedence |
| AC-9 | provider-neutral requests and Responses protocol mapping |
| AC-10 | Tool, Policy, capability, and Agent-loop orchestration |
| AC-11 | secret references, injection, and redaction |
| AC-12 | source inspection, build, activation, and rollback |
| AC-13 | Plugin SDK, manifests, locks, schemas, and templates |
| AC-14 | CLI, configuration, JSON output, and Zephyr Shell |
| AC-15 | complete non-model Linux and Zephyr scenarios |
| AC-16 | failure injection, sanitizers, coverage, and determinism |

```sh
python3 tests/check_acceptance.py \
    tests/acceptance.toml \
    build \
    --test-log build/meson-logs/testlog.json
```

## Credentialed Provider smoke

The manual `credentialed-openai-responses-smoke` is deliberately outside CI.
Build the opt-in libcurl Transport boundary and run the complete
Provider-neutral Agent loop through the OpenAI Responses Provider Plugin:

```sh
meson setup build-remote -Dopenai_smoke=true
meson compile -C build-remote

OPENAI_API_KEY='...' \
TELOS_OPENAI_MODEL='your-responses-model' \
build-remote/tests/telos-openai-smoke
```

`TELOS_OPENAI_ENDPOINT` may override the default
`https://api.openai.com/v1`. The Secret Broker resolves
`secret:provider.openai` from `OPENAI_API_KEY` only inside this trusted
diagnostic boundary. Do not put an API key in `telos.toml`, command-line
arguments, prompt text, or Plugin RPC. A successful smoke prints the completed
response plus redacted round and Tool counts. The utility is not built by
default and the credentialed request remains a manual release gate.
