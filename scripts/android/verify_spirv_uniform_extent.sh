#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
output=${1:-"$root/.local/spirv-uniform-extent-contract"}
if (($#)); then shift; fi
mkdir -p "$output"
spirv-as --target-env vulkan1.1 --preserve-numeric-ids \
    "$root/tests/device/uniform_extent_cases.spvasm" -o "$output/uniform-extent.spv"
spirv-val --target-env vulkan1.1 "$output/uniform-extent.spv"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/tests/device/test_spirv_uniform_extent.cpp" -o "$output/test_spirv_uniform_extent"
"$output/test_spirv_uniform_extent" "$output/uniform-extent.spv" "$@"
