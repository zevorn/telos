#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 BUILD_DIRECTORY" >&2
    exit 2
fi

executable=$1/zephyr/zephyr.exe
if [ ! -x "$executable" ]; then
    echo "Zephyr native simulator is missing: $executable" >&2
    exit 2
fi

output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

set +e
timeout 30s "$executable" >"$output" 2>&1
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
if [ "$passed" != true ]; then
    cat "$output" >&2
    exit 1
fi
cat "$output"
