#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/vertex-input-contract"}
mkdir -p "$out"
spirv-as --target-env vulkan1.1 "$root/tests/device/vertex_input_bits.spvasm" -o "$out/original.spv"
spirv-val --target-env vulkan1.1 "$out/original.spv"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I "$root/src/graphics/nvn_vulkan/engine" \
    "$root/tests/device/test_spirv_vertex_input.cpp" -o "$out/test_spirv_vertex_input"
arguments=("$out/original.spv" "$out")
if [[ -n "${FFRS_VERTEX_INPUT_REAL_DIR:-}" ]]; then
    for key in 11320c7ea662abd3 a4554215780cb394 00758c2888655e0b; do
        spirv-val --target-env vulkan1.1 "$FFRS_VERTEX_INPUT_REAL_DIR/$key.spv"
    done
    arguments+=("$FFRS_VERTEX_INPUT_REAL_DIR")
fi
"$out/test_spirv_vertex_input" "${arguments[@]}"
modules=("$out"/*.spv)
for module in "${modules[@]}"; do
    spirv-val --target-env vulkan1.1 "$module"
done
printf 'vertex-input validation PASS modules=%s\n' "${#modules[@]}"
