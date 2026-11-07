#!/usr/bin/env bash
# Prepare the exact open-source optimizer subset used by the optional Honor APK.
# This does not obtain or modify game data, system NDK, or another worktree.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
revision=f1e4b081af8ea65d95f0bf490a568cb657c3917e
checkout="${FFRS_SPIRV_CHECKOUT:-${root}/.local/third-party/d2r-mobile-${revision:0:7}}"
if [[ ! -e "${checkout}" ]]; then
    mkdir -p "$(dirname "${checkout}")"
    staging="$(mktemp -d "$(dirname "${checkout}")/.d2r-spirv-stage.XXXXXX")"
    printf 'Preparing pinned upstream checkout in %s\n' "${staging}" >&2
    git clone --filter=blob:none --no-checkout https://github.com/jhaizhou-ops/d2r-mobile.git "${staging}/checkout"
    git -C "${staging}/checkout" sparse-checkout init --cone
    git -C "${staging}/checkout" sparse-checkout set dm/third_party/spirv-tools
    git -C "${staging}/checkout" checkout --detach "${revision}"
    [[ ! -e "${checkout}" ]] || { echo "Target appeared during preparation; staged checkout preserved: ${staging}" >&2; exit 1; }
    mv "${staging}/checkout" "${checkout}"
    # Empty staging directory is intentionally retained for recovery/audit.
fi
[[ -d "${checkout}/.git" ]] || { echo "Not a Git checkout: ${checkout}" >&2; exit 1; }
actual="$(git -C "${checkout}" rev-parse HEAD)"
[[ "${actual}" == "${revision}" ]] || { echo "Unexpected upstream revision: ${actual}; expected ${revision}" >&2; exit 1; }
[[ -z "$(git -C "${checkout}" status --porcelain --untracked-files=all)" ]] || { echo "Upstream checkout is dirty; refusing to use it" >&2; exit 1; }
source_dir="${checkout}/dm/third_party/spirv-tools"
for f in CMakeLists.txt LICENSE README.md spirv-headers/LICENSE; do
    [[ -f "${source_dir}/${f}" ]] || { echo "Missing pinned source file ${f}" >&2; exit 1; }
done
printf '%s\n' "${source_dir}"
