#!/bin/sh
set -eu

build_root=$1
source_root=$2
installer_test=$3
abi_check=$4
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

mkdir -p "$temporary/state" "$temporary/sysroot"
DESTDIR="$temporary/sysroot" meson install -C "$build_root" >/dev/null
pkgconfig=$(find \
    "$temporary/sysroot" \
    -name telos-plugin-sdk.pc \
    -print \
    -quit)
if [ -z "$pkgconfig" ]; then
    echo "installed SDK pkg-config file is missing" >&2
    exit 1
fi

cp -R "$source_root/sdk/templates/tool" "$temporary/local-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/git-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/bad-plugin"
printf '%s\n' 'this is not valid C' \
    >>"$temporary/bad-plugin/src/plugin.c"
git -C "$temporary/git-plugin" init -q
git -C "$temporary/git-plugin" config user.name "Telos Test"
git -C "$temporary/git-plugin" config user.email "test@telos.invalid"
git -C "$temporary/git-plugin" add .
git -C "$temporary/git-plugin" commit -qm "fixture"

"$installer_test" \
    "$temporary/local-plugin" \
    "$temporary/git-plugin" \
    "$temporary/state" \
    "$(dirname "$pkgconfig")" \
    "$temporary/sysroot" \
    "$abi_check" \
    "$temporary/bad-plugin"
