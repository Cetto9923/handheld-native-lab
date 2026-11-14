#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/texel-snapshot-contract"}
mkdir -p "$out"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    "$root/tests/device/test_texel_snapshot.cpp" -o "$out/test_texel_snapshot"
"$out/test_texel_snapshot"
