#!/bin/sh
set -eu

build_root=$1
source_root=$2
abi_check=$3
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

DESTDIR="$temporary/stage" meson install -C "$build_root" >/dev/null
pkgconfig_path=$(find "$temporary/stage" -name telos-plugin-sdk.pc -print -quit)
if [ -z "$pkgconfig_path" ]; then
    echo "installed SDK pkg-config file is missing" >&2
    exit 1
fi

cp -R "$source_root/sdk/templates/tool" "$temporary/plugin"
PKG_CONFIG_PATH=$(dirname "$pkgconfig_path") \
PKG_CONFIG_SYSROOT_DIR="$temporary/stage" \
meson setup "$temporary/build" "$temporary/plugin" --wrap-mode=nodownload >/dev/null
meson compile -C "$temporary/build" >/dev/null

plugin=$(find "$temporary/build" \
    \( -name 'telos-echo-tool.so' -o -name 'telos-echo-tool.dylib' \) \
    -print \
    -quit)
if [ -z "$plugin" ]; then
    echo "SDK template did not produce a Plugin module" >&2
    exit 1
fi
"$abi_check" "$plugin"
