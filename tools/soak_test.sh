#!/bin/sh
#
# Soak test driver: run the core lifecycle probe for a duration and
# watch for memory growth, failures or hangs.
#
# Usage: soak_test.sh <soak-binary> <duration-seconds> [interval-ms]
#
# The soak binary prints a heartbeat with peak RSS every 1000 rounds;
# this driver fails when the binary exits non-zero or times out.
# Duration 0 runs forever (CI uses short durations; the 72h
# qualification run uses `soak_test.sh ./soak 259200 500`).
set -eu

binary="$1"
duration="$2"
interval="${3:-0}"

out=$(mktemp)
trap 'rm -f "$out"' EXIT

if [ "$interval" -gt 0 ] 2>/dev/null; then
    "$binary" --interval-ms "$interval" >"$out" 2>&1 &
else
    "$binary" >"$out" 2>&1 &
fi
pid=$!

start=$(date +%s)
while kill -0 "$pid" 2>/dev/null; do
    if [ "$duration" -gt 0 ]; then
        now=$(date +%s)
        if [ $((now - start)) -ge "$duration" ]; then
            kill -TERM "$pid"
            break
        fi
    fi
    sleep 2
done

set +e
wait "$pid"
status=$?
set -e
cat "$out"

if [ "$status" -ne 0 ]; then
    echo "soak: binary failed with status $status" >&2
    exit 1
fi
heartbeats=$(grep -c "peak_rss" "$out" || true)
if [ "$heartbeats" -lt 2 ] && [ "$duration" -gt 60 ]; then
    echo "soak: expected at least two heartbeats in $duration s" >&2
    exit 1
fi
echo "soak: ok, $heartbeats heartbeat line(s)"
