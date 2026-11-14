#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output=${1:-"$root/.local/nvn-command-buffer-capacity"}
mkdir -p "$output"
compiler=${CC:-clang}
"$compiler" -std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer \
    -fsanitize=address,undefined -pthread \
    "$root/tests/native/nvn_command_buffer_capacity_contract.c" -o "$output/capacity-contract"
"$output/capacity-contract"
