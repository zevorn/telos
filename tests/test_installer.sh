#!/bin/sh
set -eu

build_root=$1
source_root=$2
installer_test=$3
abi_check=$4
plugin_host=$5
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
cp -R "$source_root/sdk/templates/tool" "$temporary/container-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/bad-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/bad-test-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/bad-abi-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/bad-health-plugin"
cp -R "$source_root/sdk/templates/tool" "$temporary/slow-plugin"
printf '%s\n' 'this is not valid C' \
    >>"$temporary/bad-plugin/src/plugin.c"
printf '%s\n' "test('intentional-failure', find_program('false'))" \
    >>"$temporary/bad-test-plugin/meson.build"
sed -i \
    's/telos_plugin_init_v1/not_telos_plugin_init_v1/' \
    "$temporary/bad-abi-plugin/src/plugin.c"
sed -i \
    's/? 0 : 1;/? 1 : 1;/' \
    "$temporary/bad-health-plugin/src/plugin.c"
printf '%s\n' \
    "run_command('sh', '-c', 'sleep 3', check: true)" \
    >>"$temporary/slow-plugin/meson.build"
cat >"$temporary/container-plugin/prebuilt.c" <<'EOF'
#include <telos/plugin.h>

static int implementation;

int telos_plugin_init_v1(
    const struct telos_host_api_v1 *host,
    struct telos_plugin_registrar_v1 *registrar
)
{
    const struct telos_extension_descriptor descriptor = {
        .id = "dev.example.container-fixture",
        .kind = TELOS_EXTENSION_TOOL,
        .implementation = &implementation,
    };

    return host != NULL
        && registrar != NULL
        && registrar->add != NULL
        && registrar->add(registrar->context, &descriptor, NULL)
        ? 0
        : 1;
}
EOF
cc -shared -fPIC \
    -I"$source_root/include" \
    -o "$temporary/container-plugin/prebuilt.so" \
    "$temporary/container-plugin/prebuilt.c"
mkdir -p "$temporary/fake-bin"
cat >"$temporary/fake-bin/podman" <<'EOF'
#!/bin/sh
set -eu

source_directory=
staging_directory=
for argument do
    case "$argument" in
        *:/workspace:ro)
            source_directory=${argument%:/workspace:ro}
            ;;
        *:/staging:rw)
            staging_directory=${argument%:/staging:rw}
            ;;
    esac
done
test -n "$source_directory"
test -n "$staging_directory"
mkdir -p "$staging_directory/lib/telos/plugins"
cp \
    "$source_directory/prebuilt.so" \
    "$staging_directory/lib/telos/plugins/container-fixture.so"
EOF
chmod +x "$temporary/fake-bin/podman"
git -C "$temporary/git-plugin" init -q
git -C "$temporary/git-plugin" config user.name "Telos Test"
git -C "$temporary/git-plugin" config user.email "test@telos.invalid"
git -C "$temporary/git-plugin" add .
git -C "$temporary/git-plugin" commit -qm "fixture"

PATH="$temporary/fake-bin:$PATH" "$installer_test" \
    "$temporary/local-plugin" \
    "$temporary/git-plugin" \
    "$temporary/state" \
    "$(dirname "$pkgconfig")" \
    "$temporary/sysroot" \
    "$abi_check" \
    "$plugin_host" \
    "$temporary/container-plugin" \
    "$temporary/bad-plugin" \
    "$temporary/bad-test-plugin" \
    "$temporary/bad-abi-plugin" \
    "$temporary/bad-health-plugin" \
    "$temporary/slow-plugin"
