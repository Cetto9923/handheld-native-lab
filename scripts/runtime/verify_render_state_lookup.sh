#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/render-state-lookup-contract"}
mkdir -p "$out"
"${CC:-clang}" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -pthread \
    "$root/tests/native/nvn_render_state_lookup_contract.c" \
    "$root/src/graphics/nvn_device/nvn_render_state.c" \
    -o "$out/render_state_lookup"
"$out/render_state_lookup"
