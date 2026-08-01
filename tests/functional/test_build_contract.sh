#!/bin/sh
set -eu

source_root=$1
generator=$2
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$generator" \
    --manifest "$source_root/platforms/zephyr/components.toml" \
    --output "$source_root/platforms/zephyr/zephyr/generated_sources.cmake" \
    --check

if meson setup "$temporary/abi" "$source_root" \
    -Dtests=false \
    -Dplugin_abi=2 \
    >"$temporary/abi.log" 2>&1
then
    echo "unsupported Plugin ABI configured successfully" >&2
    exit 1
fi
grep -q "supports Plugin ABI version 1 only" "$temporary/abi.log"

if meson setup "$temporary/c11" "$source_root" \
    -Dtests=false \
    -Dc_std=c11 \
    >"$temporary/c11.log" 2>&1
then
    echo "unsupported C standard configured successfully" >&2
    exit 1
fi
grep -q "requires the C17 language standard" "$temporary/c11.log"
