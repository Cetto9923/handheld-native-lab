#!/usr/bin/env bash
# C11/API28 ARM64 build. The default cache is a partial official NDK, not a full NDK.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${FFRS_VK_TOOLCHAIN:-${ROOT}/.local/minimal-ndk/toolchain}"
MEMBERS="${FFRS_VK_MEMBERS:-${ROOT}/.local/minimal-ndk/validated-members.json}"
RUST_ROOT="${FFRS_VK_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
LINKER="${RUST_ROOT}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
CC="${FFRS_VK_CC:-$(command -v clang)}"
SYSROOT="${TOOLCHAIN}/sysroot"
RESOURCE="${TOOLCHAIN}/lib/clang/18"
API_LIBS="${SYSROOT}/usr/lib/aarch64-linux-android/28"
BUILTINS="${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a"
SOURCE="${ROOT}/src/graphics/vulkan_probe/vulkan_transfer_probe.c"
RUN="${FFRS_VK_BUILD_DIR:-${ROOT}/.local/vulkan-ops/$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ ! -f "${MEMBERS}" || ! -f "${SYSROOT}/usr/include/vulkan/vulkan_core.h" ||
      ! -x "${LINKER}" || ! -x "${TOOLCHAIN}/bin/llvm-readelf" || ! -f "${BUILTINS}" ]]; then
    echo 'BLOCKED_TOOLCHAIN: explicit validated-members manifest, headers, builtins, LLD and readelf required.' >&2
    exit 2
fi
if [[ -e "${RUN}" ]]; then
    echo "Refusing to overwrite existing build evidence: ${RUN}" >&2
    exit 2
fi
mkdir -p "${RUN}/logs" "${RUN}/source_freeze/src/graphics/vulkan_probe" \
    "${RUN}/source_freeze/src/graphics/vulkan" "${RUN}/source_freeze/scripts/graphics"
cp "${SOURCE}" "${RUN}/source_freeze/src/graphics/vulkan_probe/"
cp "${ROOT}/src/graphics/vulkan/buffer_resource.c" "${ROOT}/src/graphics/vulkan/buffer_resource.h" \
    "${RUN}/source_freeze/src/graphics/vulkan/"
cp "${BASH_SOURCE[0]}" "${RUN}/source_freeze/scripts/graphics/"
cp "${ROOT}/scripts/graphics/validate_vulkan_artifact.py" "${RUN}/source_freeze/scripts/graphics/"
git -C "${ROOT}" rev-parse HEAD > "${RUN}/git-head.txt"
git -C "${ROOT}" status --short > "${RUN}/git-status.txt"
git -C "${ROOT}" diff -- src/graphics/vulkan src/graphics/vulkan_probe scripts/graphics tests/graphics \
    docs/analysis/codex_vulkan_buffer_report.md > "${RUN}/tracked-source.diff"
run_step() {
    local label="$1" code=0
    shift
    printf '%q ' "$@" > "${RUN}/logs/${label}.command"
    printf '\n' >> "${RUN}/logs/${label}.command"
    "$@" > "${RUN}/logs/${label}.log" 2>&1 || code=$?
    printf '%s\n' "${code}" > "${RUN}/logs/${label}.exitcode"
    cat "${RUN}/logs/${label}.log"
    return "${code}"
}
SOURCE_SHA="$(python3 -c 'import runpy,sys; print(runpy.run_path(sys.argv[1])["source_bundle"](sys.argv[2])[0])' \
    "${RUN}/source_freeze/scripts/graphics/validate_vulkan_artifact.py" "${RUN}/source_freeze")"
GIT_HEAD="$(cat "${RUN}/git-head.txt")"
run_step compiler-version "${CC}" --version
run_step compile "${CC}" --target=aarch64-linux-android28 --sysroot="${SYSROOT}" \
    -resource-dir "${RESOURCE}" -isystem "${SYSROOT}/usr/include/aarch64-linux-android" \
    -isystem "${SYSROOT}/usr/include/vulkan" -std=c11 -O2 -g -fPIE -fno-omit-frame-pointer \
    -Wall -Wextra -Werror -Wpedantic -MD -MF "${RUN}/probe.d" \
    "-DFFRS_VK_SOURCE_SHA256=\"${SOURCE_SHA}\"" "-DFFRS_VK_GIT_HEAD=\"${GIT_HEAD}\"" \
    -c "${RUN}/source_freeze/src/graphics/vulkan_probe/vulkan_transfer_probe.c" -o "${RUN}/probe.o"
run_step compile-buffer "${CC}" --target=aarch64-linux-android28 --sysroot="${SYSROOT}" \
    -resource-dir "${RESOURCE}" -isystem "${SYSROOT}/usr/include/aarch64-linux-android" \
    -isystem "${SYSROOT}/usr/include/vulkan" -std=c11 -O2 -g -fPIE -fno-omit-frame-pointer \
    -Wall -Wextra -Werror -Wpedantic -MD -MF "${RUN}/buffer_resource.d" \
    -c "${RUN}/source_freeze/src/graphics/vulkan/buffer_resource.c" -o "${RUN}/buffer_resource.o"
run_step link env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
    --no-undefined --fatal-warnings --build-id=sha1 --eh-frame-hdr \
    -pie --dynamic-linker /system/bin/linker64 -z max-page-size=16384 -z noexecstack -z relro -z now \
    -o "${RUN}/vulkan_transfer_probe" "${API_LIBS}/crtbegin_dynamic.o" "${RUN}/probe.o" "${RUN}/buffer_resource.o" \
    --start-group "${BUILTINS}" -L "${API_LIBS}" -lc -lvulkan --end-group "${API_LIBS}/crtend_android.o"
run_step elf "${TOOLCHAIN}/bin/llvm-readelf" --file-header --program-headers --dynamic-table \
    "${RUN}/vulkan_transfer_probe"
run_step buffer-symbols "${TOOLCHAIN}/bin/llvm-readelf" --symbols "${RUN}/buffer_resource.o"
run_step evidence python3 "${RUN}/source_freeze/scripts/graphics/validate_vulkan_artifact.py" \
    --build-dir "${RUN}" --toolchain "${TOOLCHAIN}" --members "${MEMBERS}" \
    --compiler "${CC}" --linker "${LINKER}" --builtins "${BUILTINS}"
printf 'Android execution: NOT_RUN\nELF and evidence: %s\n' "${RUN}"
