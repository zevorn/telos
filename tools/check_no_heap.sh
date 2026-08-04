#!/bin/sh
#
# No-heap path gate: critical-path sources must never allocate.
#
# Enforces two invariants on every file in noheap.list:
#   1. the file includes <telos/no_heap.h> (compile-time enforcement
#      via -DTELOS_NO_HEAP in the meson build)
#   2. outside comments, no malloc/calloc/realloc/free call appears
#
# Usage: check_no_heap.sh <noheap.list> <source-root>
set -u

list="$1"
root="$2"
failures=0

while IFS= read -r file; do
    case "$file" in
        ''|\#*) continue ;;
    esac
    path="$root/$file"
    if [ ! -f "$path" ]; then
        echo "no-heap gate: missing file $file" >&2
        failures=$((failures + 1))
        continue
    fi
    if ! grep -q '#include <telos/no_heap.h>' "$path"; then
        echo "no-heap gate: $file lacks #include <telos/no_heap.h>" >&2
        failures=$((failures + 1))
    fi
    # Strip /* */ comments and // comments, then look for heap calls.
    if sed 's,/\*.*\*/, ,g; s,//.*$,,' "$path" \
        | grep -qE '\b(malloc|calloc|realloc|free)[[:space:]]*\('; then
        echo "no-heap gate: heap call in $file" >&2
        failures=$((failures + 1))
    fi
done < "$list"

count=$(grep -cvE '^\s*(#|$)' "$list" || true)
if [ "$failures" -ne 0 ]; then
    echo "no-heap gate: $failures violation(s)" >&2
    exit 1
fi
echo "no-heap gate: $count file(s) clean"
