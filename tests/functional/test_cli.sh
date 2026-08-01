#!/bin/sh
set -eu

build_root=$1
source_root=$2
telos=$3
abi_check=$4
plugin_host=$5
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

mkdir -p \
    "$temporary/fake-bin" \
    "$temporary/home" \
    "$temporary/project" \
    "$temporary/state" \
    "$temporary/prepared-state" \
    "$temporary/sysroot" \
    "$temporary/resources/example"
printf '%s\n' \
    '#!/bin/sh' \
    "printf '%s\\n' \"\$@\" >'$temporary/container.args'" \
    'exit 99' \
    >"$temporary/fake-bin/podman"
cp "$temporary/fake-bin/podman" "$temporary/fake-bin/docker"
chmod +x \
    "$temporary/fake-bin/podman" \
    "$temporary/fake-bin/docker"
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
    --plugin-host "$plugin_host" \
    plugin install "$source_root/sdk/templates/tool" \
    | grep -q '"id":"dev.example.echo-tool"'
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/state" \
    plugin list \
    | grep -q '"dev.example.echo-tool"'

HOME="$temporary/home" "$telos" \
    --json \
    --yes \
    --builder native \
    --state-dir "$temporary/prepared-state" \
    --sdk-pkgconfig "$(dirname "$pkgconfig")" \
    --sdk-sysroot "$temporary/sysroot" \
    --abi-check "$abi_check" \
    --plugin-host "$plugin_host" \
    plugin build "$source_root/sdk/templates/tool" \
    | grep -q '"ok":true'
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/prepared-state" \
    plugin list \
    | grep -q '"plugins":\[\]'

if PATH="$temporary/fake-bin:$PATH" HOME="$temporary/home" "$telos" \
    --json \
    --yes \
    --builder container \
    --state-dir "$temporary/prepared-state" \
    --sdk-pkgconfig "$(dirname "$pkgconfig")" \
    --sdk-sysroot "$temporary/sysroot" \
    --abi-check "$abi_check" \
    --plugin-host "$plugin_host" \
    plugin build "$source_root/sdk/templates/tool" \
    >"$temporary/container-error.json"
then
    echo "failing container Builder unexpectedly succeeded" >&2
    exit 1
fi
grep -q '"ok":false' "$temporary/container-error.json"
grep -q -- '--network=none' "$temporary/container.args"
grep -q -- '--read-only' "$temporary/container.args"
grep -q -- '--cap-drop=ALL' "$temporary/container.args"
grep -q -- ':ro' "$temporary/container.args"
if grep -q -- '/var/run/docker.sock' "$temporary/container.args"; then
    echo "container Builder mounted the container socket" >&2
    exit 1
fi
cache_path=$(find \
    "$temporary/prepared-state/cache/plugins" \
    -mindepth 1 \
    -maxdepth 1 \
    -type d \
    -print \
    -quit)
cache_key=${cache_path##*/}
test -n "$cache_key"
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/prepared-state" \
    plugin activate dev.example.echo-tool "$cache_key" \
    | grep -q '"action":"activated"'
HOME="$temporary/home" "$telos" \
    --json \
    --yes \
    --builder native \
    --state-dir "$temporary/prepared-state" \
    --sdk-pkgconfig "$(dirname "$pkgconfig")" \
    --sdk-sysroot "$temporary/sysroot" \
    --abi-check "$abi_check" \
    --plugin-host "$plugin_host" \
    plugin test "$source_root/sdk/templates/tool" \
    | grep -q '"ok":true'
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/prepared-state" \
    plugin remove dev.example.echo-tool \
    | grep -q '"action":"removed"'
HOME="$temporary/home" "$telos" \
    --json \
    --state-dir "$temporary/prepared-state" \
    plugin list \
    | grep -q '"plugins":\[\]'
