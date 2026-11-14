#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output=${1:-"$repo/.local/integer-buffer-contract"}
mkdir -p "$output"

spirv-as --target-env vulkan1.1 "$repo/tests/device/reflection_uint_fetch.spvasm" -o "$output/original.spv"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$repo/tests/device/test_reflection_uint_fetch.cpp" -o "$output/test_integer_buffer"
"$output/test_integer_buffer" "$output/original.spv" "$output/strict.spv"
for module in "$output"/*.spv; do
    spirv-val --target-env vulkan1.1 "$module"
done

# Optional game modules stay local. Each binding uses the production API and
# fallback transformations also undergo a function-instruction comparison.
rewrite_real() {
    source=$1
    destination=$2
    shift 2
    [ -n "$source" ] || return 0
    spirv-val --target-env vulkan1.1 "$source"
    "$output/test_integer_buffer" --module "$source" "$output/$destination.spv" "$@"
    spirv-val --target-env vulkan1.1 "$output/$destination.spv"
}
rewrite_real "${FFRS_CASTBACK_5BF_MODULE:-}" real-5bf 17 23
rewrite_real "${FFRS_CASTBACK_924_MODULE:-}" real-924 29
rewrite_real "${FFRS_CASTBACK_7CA_MODULE:-}" real-7ca 12 18 24
rewrite_real "${FFRS_CASTBACK_D56_MODULE:-}" real-d56 11 17 23 29
