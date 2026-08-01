#!/bin/sh
set -eu

unset CDPATH
source_root=$(cd -- "$(dirname "$0")/.." && pwd)
build_directory=${1:-build-coverage}
cd "$source_root"

case "$build_directory" in
    ""|"/"|"$source_root")
        echo "unsafe coverage build directory: $build_directory" >&2
        exit 2
        ;;
esac

if [ -f "$build_directory/meson-private/coredata.dat" ]; then
    meson setup --wipe "$build_directory" "$source_root" -Db_coverage=true
else
    meson setup "$build_directory" "$source_root" -Db_coverage=true
fi

meson compile -C "$build_directory"
meson test -C "$build_directory" --print-errorlogs

coverage_directory=$build_directory/coverage
mkdir -p "$coverage_directory"
gcovr \
    --root "$source_root" \
    "$build_directory/src" \
    "$build_directory/plugins" \
    "$build_directory/platforms/linux" \
    --filter '^(src|plugins|platforms/linux)/' \
    --html-details "$coverage_directory/index.html" \
    --xml "$coverage_directory/coverage.xml" \
    --json-summary "$coverage_directory/summary.json" \
    --txt "$coverage_directory/summary.txt" \
    --print-summary \
    --fail-under-line 90 \
    --fail-under-branch 80
