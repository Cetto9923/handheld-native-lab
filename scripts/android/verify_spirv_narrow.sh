#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/spirv-narrow-contract"}
mkdir -p "$out"
spirv-as --target-env vulkan1.1 "$root/tests/device/warp_reduce.spvasm" -o "$out/original.spv"
spirv-val --target-env vulkan1.1 "$out/original.spv"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I "$root/src/graphics/nvn_vulkan/engine" \
    "$root/tests/device/test_spirv_narrow.cpp" -o "$out/test_spirv_narrow"
arguments=("$out/original.spv" "$out")
if [[ -n "${FFRS_NARROW_REAL_MODULE_DIR:-}" ]]; then
    for key in 2821e3f501f9f32d 3170847483da87c5 4885572240c3ad2d 876dd0820a7ad8fa b673e10f3da2451c; do
        arguments+=("$FFRS_NARROW_REAL_MODULE_DIR/$key-original.spv")
    done
fi
"$out/test_spirv_narrow" "${arguments[@]}"
spirv-val --target-env vulkan1.1 "$out/narrow-8.spv"
spirv-val --target-env vulkan1.1 "$out/narrow-16.spv"
for name in unrelated-xor unrelated-predicate unsupported-down unsupported-combine aliased-predicate overwrite-alias copy-memory wide-index out-pointer-write; do
    spirv-val --target-env vulkan1.1 "$out/$name-source.spv"
    spirv-val --target-env vulkan1.1 "$out/$name-refused.spv"
done
if [[ -n "${FFRS_NARROW_REAL_MODULE_DIR:-}" ]]; then
    for key in 2821e3f501f9f32d 3170847483da87c5 4885572240c3ad2d 876dd0820a7ad8fa b673e10f3da2451c; do
        spirv-val --target-env vulkan1.1 "$FFRS_NARROW_REAL_MODULE_DIR/$key-original.spv"
    done
    for index in 0 1 2 3 4; do
        spirv-val --target-env vulkan1.1 "$out/real-$index.spv"
    done
fi
