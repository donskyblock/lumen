#!/usr/bin/env bash
# Configure (when needed) and build Lumen from the repository root.
set -euo pipefail

# Note the fact that this is written purely because im lazy and could not be assed to write CMAKE everytime 

cd "$(dirname "$0")"

build_dir="${LUMEN_BUILD_DIR:-build}"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

cmake -S . -B "$build_dir" -G Ninja
cmake --build "$build_dir" --parallel "$jobs"
