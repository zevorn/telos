#!/bin/sh

set -eu

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
file_list=$(mktemp "${TMPDIR:-/tmp}/telos-style.XXXXXX")
trap 'rm -f "$file_list"' EXIT HUP INT TERM

if [ "${1:-}" = "--all" ]; then
    shift
    git -C "$source_root" ls-files --cached --others --exclude-standard \
        -- '*.c' '*.h' > "$file_list"
elif [ "$#" -gt 0 ]; then
    for path in "$@"; do
        printf '%s\n' "$path"
    done > "$file_list"
else
    {
        git -C "$source_root" diff --name-only --diff-filter=ACMR HEAD \
            -- '*.c' '*.h'
        git -C "$source_root" ls-files --others --exclude-standard \
            -- '*.c' '*.h'
    } | sort -u > "$file_list"
fi

status=0
while IFS= read -r path; do
    checkpatch_output=

    [ -n "$path" ] || continue
    case "$path" in
        /*) file=$path ;;
        *) file=$source_root/$path ;;
    esac
    [ -f "$file" ] || continue
    if ! python3 "$source_root/scripts/check-function-layout.py" "$file"; then
        status=1
    fi
    if ! checkpatch_output=$(perl "$source_root/scripts/checkpatch.pl" \
        --no-tree --no-signoff --strict --quiet --terse --no-summary \
        --file "$file" 2>&1); then
        checkpatch_output=$(printf '%s\n' "$checkpatch_output" | sed \
            '/consider using qemu_strto.* in preference to/d')
        if [ -n "$checkpatch_output" ]; then
            printf '%s\n' "$checkpatch_output" >&2
            status=1
        fi
    fi
done < "$file_list"

exit "$status"
