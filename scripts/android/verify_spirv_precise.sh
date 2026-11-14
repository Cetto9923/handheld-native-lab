#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/spirv-precise-contract"}
mkdir -p "$out"
spirv-as --target-env vulkan1.1 "$root/tests/device/position_precision.spvasm" -o "$out/original.spv"
spirv-val --target-env vulkan1.1 "$out/original.spv"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I "$root/src/graphics/nvn_vulkan/engine" \
    "$root/tests/device/test_spirv_precise.cpp" -o "$out/test_spirv_precise"
arguments=("$out/original.spv" "$out")
for module in "${FFRS_PRECISE_REAL_MODULE:-}" "${FFRS_PRECISE_CASTBACK_MODULE:-}"; do
    if [[ -n "$module" ]]; then
        spirv-val --target-env vulkan1.1 "$module"
        arguments+=("$module")
    fi
done
"$out/test_spirv_precise" "${arguments[@]}"
modules=("$out"/*.spv)
for module in "${modules[@]}"; do
    spirv-val --target-env vulkan1.1 "$module"
done
printf 'precise validation PASS modules=%s\n' "${#modules[@]}"
