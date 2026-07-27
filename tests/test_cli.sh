#!/bin/sh
set -eu

build_root=$1
source_root=$2
telos=$3
abi_check=$4
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

mkdir -p \
    "$temporary/home" \
    "$temporary/project" \
    "$temporary/state" \
    "$temporary/sysroot" \
    "$temporary/resources/example"
DESTDIR="$temporary/sysroot" meson install -C "$build_root" >/dev/null
pkgconfig=$(find \
    "$temporary/sysroot" \
    -name telos-plugin-sdk.pc \
    -print \
    -quit)

HOME="$temporary/home" "$telos" --help | grep -q "plugin list|info|install"
HOME="$temporary/home" "$telos" --json doctor \
    | grep -q '"ok":true'
if HOME="$temporary/home" "$telos" --json invalid \
    >"$temporary/invalid.json"
then
    echo "invalid CLI command succeeded" >&2
    exit 1
fi
grep -q '"ok":false' "$temporary/invalid.json"

HOME="$temporary/home" "$telos" --json plugin info \
    "$source_root/sdk/templates/tool" \
    | grep -q '"id":"dev.example.echo-tool"'

printf '%s\n' \
    '---' \
    'name: example' \
    'description: Example Skill' \
    '---' \
    'Use the example.' \
    >"$temporary/resources/example/SKILL.md"
HOME="$temporary/home" "$telos" --json resource validate \
    "$temporary/resources" \
    | grep -q '"skills":1'

HOME="$temporary/home" "$telos" \
    --json \
    --yes \
    --builder native \
    --state-dir "$temporary/state" \
    --sdk-pkgconfig "$(dirname "$pkgconfig")" \
    --sdk-sysroot "$temporary/sysroot" \
    --abi-check "$abi_check" \
    plugin install "$source_root/sdk/templates/tool" \
    | grep -q '"id":"dev.example.echo-tool"'
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/state" \
    plugin list \
    | grep -q '"dev.example.echo-tool"'
