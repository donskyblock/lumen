#!/usr/bin/env bash
# Build and install Lumen for the current user (or under PREFIX).
set -euo pipefail
cd "$(dirname "$0")"

prefix="${PREFIX:-${HOME}/.local}"
build_dir="${LUMEN_BUILD_DIR:-build}"

if [[ ! -f "$build_dir/build.ninja" && ! -f "$build_dir/Makefile" ]]; then
    ./build.sh
else
    cmake --build "$build_dir" --parallel
fi
cmake --install "$build_dir" --prefix "$prefix"

if [[ "$prefix" == "$HOME/.local" ]]; then
    applications_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
else
    applications_dir="$prefix/share/applications"
fi
mkdir -p "$applications_dir"
desktop_tmp="$(mktemp)"
trap 'rm -f "$desktop_tmp"' EXIT
sed "s|^Exec=lumen$|Exec=\"$prefix/bin/lumen\"|" packaging/lumen.desktop > "$desktop_tmp"
install -m 644 "$desktop_tmp" "$applications_dir/lumen.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
fi
printf 'Installed Lumen to %s/bin/lumen\nLauncher: %s/lumen.desktop\n' "$prefix" "$applications_dir"
