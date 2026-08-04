#!/bin/sh
set -eu
TELOS="${1:-build/tools/telos}"
OUTPUT="/tmp/telos-smoke-$$.txt"
trap "rm -f $OUTPUT" EXIT

echo "=== TUI Smoke Test ==="
echo "Binary: $TELOS"
[ -x "$TELOS" ] || { echo "FAIL: binary not found"; exit 1; }

echo "Running: $TELOS run ..."
"$TELOS" run 'Say exactly OK and nothing else.' > "$OUTPUT" 2>&1 || true

echo "=== Output ==="
cat "$OUTPUT"
echo "=== Analysis ==="
EXIT=0

if grep -qi "round limit\|exceeded.*round\|Agent loop exceeded" "$OUTPUT"; then
    echo "FAIL: round limit"; EXIT=1
else
    echo "PASS: no round limit"
fi

if grep -q "Telos\|OK" "$OUTPUT"; then
    echo "PASS: agent responded"
else
    echo "FAIL: no response"; EXIT=1
fi

if grep -qi "SIGABRT\|Assertion failed\|abort" "$OUTPUT"; then
    echo "FAIL: fatal error"; EXIT=1
else
    echo "PASS: no fatal errors"
fi

[ $EXIT -eq 0 ] && echo "=== ALL PASSED ===" || echo "=== FAILED ==="
exit $EXIT
