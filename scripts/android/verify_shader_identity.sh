#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/shader-identity-contract"}
mkdir -p "$out"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$root/tests/device/test_shader_identity.cpp" -o "$out/test_shader_identity"
if [[ -n "${FFRS_IDENTITY_REAL_MODULE_DIR:-}" ]]; then
    "$out/test_shader_identity" "$FFRS_IDENTITY_REAL_MODULE_DIR" "$out"
    for key in 5bf9a939f45a8bbf d56a8ffe79793ace; do
        spirv-val --target-env vulkan1.1 "$FFRS_IDENTITY_REAL_MODULE_DIR/$key-original.spv"
        spirv-val --target-env vulkan1.1 "$out/$key-combined.spv"
        spirv-val --target-env vulkan1.1 "$out/$key-typed-only.spv"
    done
else
    "$out/test_shader_identity"
fi
