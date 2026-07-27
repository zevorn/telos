#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 BUILD_DIRECTORY [QEMU_SYSTEM_AARCH64]" >&2
    exit 2
fi

kernel=$1/zephyr/zephyr.elf
qemu=${2:-qemu-system-aarch64}
if [ ! -f "$kernel" ]; then
    echo "Zephyr ARM image is missing: $kernel" >&2
    exit 2
fi
if ! command -v "$qemu" >/dev/null 2>&1; then
    echo "QEMU executable is unavailable: $qemu" >&2
    exit 2
fi

output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

set +e
timeout 30s "$qemu" \
    -global virtio-mmio.force-legacy=false \
    -cpu cortex-a53 \
    -machine virt,secure=on,gic-version=3 \
    -nic user,model=e1000 \
    -chardev stdio,id=con,mux=on \
    -serial chardev:con \
    -mon chardev=con,mode=readline \
    -nographic \
    -icount shift=4,align=off,sleep=on \
    -rtc clock=vm \
    -kernel "$kernel" \
    >"$output" 2>&1
status=$?
set -e

case "$status" in
    0|124)
        ;;
    *)
        cat "$output" >&2
        exit "$status"
        ;;
esac

passed=true
grep -Fq "Telos Agentic Framework: ready" "$output" || passed=false
grep -Fq "TELOS_SCENARIO: PASSED" "$output" || passed=false
grep -Fq \
    "trace: static echo output=zephyr state=COMPLETED" \
    "$output" \
    || passed=false
grep -Fq "TELOS_NETWORK: READY " "$output" || passed=false
if [ "$passed" != true ]; then
    cat "$output" >&2
    exit 1
fi
cat "$output"
