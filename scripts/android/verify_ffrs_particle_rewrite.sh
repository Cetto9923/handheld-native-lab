#!/usr/bin/env bash
# Real game modules remain external fixtures. Outputs stay in the local tree.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fixtures="${1:-${FFRS_PARTICLE_FIXTURES:-${root}/.local/shrine-oom-20261111/run11/spv-dump}}"
out="${root}/.local/worker-particle-20261114/regression"
cxx="${CXX:-clang++}"
validator="${SPIRV_VAL:-spirv-val}"
keys=(9198ac8854711e1c ead249f088784a89 cc083b4b242a59ba d83a8836c4cfac88 8574fb4cb432cb74)

command -v "${cxx}" >/dev/null
command -v "${validator}" >/dev/null
for key in "${keys[@]}"; do
    if [[ ! -f "${fixtures}/${key}.spv" ]]; then
        echo "MISSING_FIXTURE ${fixtures}/${key}.spv" >&2
        exit 2
    fi
done
mkdir -p "${out}/modules"

{
    printf 'cwd=%q\n' "${root}"
    printf 'fixtures=%q\n' "${fixtures}"
    printf 'compiler=%q\nvalidator=%q\n' "${cxx}" "${validator}"
    git -C "${root}" rev-parse HEAD
    git -C "${root}" status --short
    "${cxx}" --version
    "${validator}" --version
} > "${out}/inputs.log"

"${cxx}" -std=c++17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    "${root}/tests/device/test_ffrs_particle_rewrite.cpp" \
    -o "${out}/test-ffrs-particle-rewrite" > "${out}/build.log" 2>&1
"${out}/test-ffrs-particle-rewrite" "${fixtures}" "${out}/modules" \
    > "${out}/contract.log" 2>&1
cat "${out}/contract.log"

: > "${out}/spirv-val.log"
for key in "${keys[@]}"; do
    "${validator}" --target-env vulkan1.1 "${fixtures}/${key}.spv" >> "${out}/spirv-val.log" 2>&1
    "${validator}" --target-env vulkan1.1 "${out}/modules/${key}.spv" >> "${out}/spirv-val.log" 2>&1
    printf '%s original_and_rewritten_valid=1\n' "${key}" >> "${out}/spirv-val.log"
done
cat "${out}/spirv-val.log"
python3 - "${fixtures}" "${out}/modules" "${keys[@]}" > "${out}/modules.json" <<'PY'
import hashlib
import json
import pathlib
import sys

records = []
for key in sys.argv[3:]:
    for role, directory in (("original", sys.argv[1]), ("rewritten", sys.argv[2])):
        path = pathlib.Path(directory, key + ".spv").resolve()
        data = path.read_bytes()
        records.append({
            "key": key,
            "role": role,
            "path": str(path),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
print(json.dumps(records, indent=2))
PY
echo "PASS host particle entry-point contract and SPIR-V validation"
echo "logs=${out}"
