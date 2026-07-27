# Telos verification

Telos keeps remote model credentials out of automated tests. Everything below
the credentialed HTTPS boundary is exercised with deterministic fixtures:
request construction, streaming Responses parsing, local and remote state,
Tool Calls, Policy decisions, process Plugins, persistence, and failure paths.

## Linux

Run a clean GCC build:

```sh
CC=gcc meson setup --wipe build-gcc
meson compile -C build-gcc
meson test -C build-gcc --print-errorlogs
python3 tools/check_acceptance.py \
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
tools/check_coverage.sh build-coverage
```

The report covers all production files below `src/`. Tests are the only
excluded path. The gate requires at least 90% line coverage and 80% branch
coverage. The `allocation-failure-matrix` test injects failures into every
observed `malloc`, `calloc`, and `realloc` call for the public subsystems it
exercises.

## Zephyr

The repository contains a west manifest pinned to Zephyr 4.4.0. From a west
workspace whose self project is `telos`, build and run the native simulator:

```sh
west build -p always \
    -d build-zephyr-native \
    -b native_sim/native/64 \
    telos/samples/telos_agent \
    -- -DZEPHYR_EXTRA_MODULES="$PWD/telos/platforms/zephyr"
telos/tools/run_zephyr_native.sh build-zephyr-native
```

Build and run the ARM Virt scenario:

```sh
west build -p always \
    -d build-zephyr-qemu \
    -b qemu_cortex_a53 \
    telos/samples/telos_agent \
    -- -DZEPHYR_EXTRA_MODULES="$PWD/telos/platforms/zephyr"
telos/tools/run_zephyr_qemu.sh build-zephyr-qemu
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

```sh
python3 tools/check_acceptance.py \
    tests/acceptance.toml \
    build \
    --test-log build/meson-logs/testlog.json
```

## Credentialed Provider smoke

The manual `credentialed-openai-responses-smoke` is deliberately outside CI.
Build the opt-in libcurl Transport boundary and run the complete
Provider-neutral Agent loop through the OpenAI Responses adapter:

```sh
meson setup build-remote -Dopenai_smoke=true
meson compile -C build-remote

OPENAI_API_KEY='...' \
TELOS_OPENAI_MODEL='your-responses-model' \
build-remote/tools/telos-openai-smoke
```

`TELOS_OPENAI_ENDPOINT` may override the default
`https://api.openai.com/v1`. The Secret Broker resolves
`secret:provider.openai` from `OPENAI_API_KEY` only inside this trusted
diagnostic boundary. Do not put an API key in `telos.toml`, command-line
arguments, prompt text, or Plugin RPC. A successful smoke prints the completed
response plus redacted round and Tool counts. The utility is not built by
default and the credentialed request remains a manual release gate.
