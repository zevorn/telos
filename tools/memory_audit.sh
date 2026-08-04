#!/bin/sh
#
# Memory audit gate: no leaks and no unbounded growth in the core.
#
# Runs a bounded soak and checks:
#   1. the binary completed every round (any API failure fails)
#   2. peak RSS at the end is not growing beyond a trend threshold
#      (an unbounded leak pushes peak RSS up every heartbeat)
#
# For leak detection run it against an ASan build:
#   meson setup build-asan -Db_sanitize=address
#   tools/memory_audit.sh ./build-asan/tests/functional/soak 5000
#
# Usage: memory_audit.sh <soak-binary> <rounds>
set -eu

binary="$1"
rounds="$2"
threshold_percent=25

output=$("$binary" --rounds "$rounds" 2>&1)
status=$?
printf '%s\n' "$output"
if [ "$status" -ne 0 ]; then
    echo "memory audit: soak failed (leaks or API failure)" >&2
    exit 1
fi
if ! printf '%s\n' "$output" | grep -q "soak completed: $rounds rounds"; then
    echo "memory audit: soak did not complete $rounds rounds" >&2
    exit 1
fi

first_rss=$(printf '%s\n' "$output" |
    sed -n 's/.*peak_rss \([0-9]*\)KB.*/\1/p' | head -1)
last_rss=$(printf '%s\n' "$output" |
    sed -n 's/.*peak_rss \([0-9]*\)KB.*/\1/p' | tail -1)
if [ -n "$first_rss" ] && [ -n "$last_rss" ] &&
    [ "$first_rss" -gt 0 ]; then
    growth=$(( (last_rss - first_rss) * 100 / first_rss ))
    if [ "$growth" -gt "$threshold_percent" ]; then
        echo "memory audit: peak RSS grew ${growth}% over $rounds rounds" >&2
        exit 1
    fi
    echo "memory audit: peak RSS growth ${growth}% (first ${first_rss}KB, last ${last_rss}KB)"
fi
echo "memory audit: $rounds rounds clean"
