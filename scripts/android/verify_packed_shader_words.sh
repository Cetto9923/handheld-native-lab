#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-"$root/.local/packed-shader-words-contract"}
mkdir -p "$out"
python3 - "$root" "$out" <<'PY'
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
source = (root / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
begin = source.index('bool keep_rewritten=false,reuse_rejected=false;')
end = source.index('VkShaderModule load_module(', begin)
(out / 'rewrite_cache_production.inc').write_text('std::mutex mu;\n' + source[begin:end])
PY
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined -pthread \
    -I "$root/src/graphics/nvn_vulkan/engine" -I "$out" \
    "$root/tests/device/test_packed_shader_words.cpp" -lz -o "$out/test_packed_shader_words"
arguments=("$out/test_packed_shader_words")
for module in "${FFRS_PRECISE_REAL_MODULE:-}" "${FFRS_PRECISE_CASTBACK_MODULE:-}" "${FFRS_REAL_TEXEL_MODULE:-}"; do
    if [[ -n "$module" ]]; then arguments+=("$module"); fi
done
"${arguments[@]}"
