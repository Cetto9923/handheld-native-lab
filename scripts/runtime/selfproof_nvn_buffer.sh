#!/usr/bin/env bash
# The NVN buffer contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims a size the guest never set),
#       the final gate kept                                  -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                            -> the false green
#   C = the contract untouched, the buffer family stops copying the builder's
#       description into the buffer                        -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction: a
# buffer is a snapshot of the builder it was made from, so a family that does
# not copy the description would answer with values the guest never set.
#
# The baseline run at the end is the unmutated contract with the real family:
# it has to be green, otherwise the three variants prove nothing.
#
# The dispatch of the family in src/graphics/nvn_device/nvn_device.c belongs to
# the coordinator. The contract resolves the names through the guest's own
# lookup, so this script builds a copy of that file with the family wired in
# and uses the copy for the runs. The file in the tree is not touched.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

# The build directory of this self proof. It does not match the
# .local/build-p-mm-* pattern on purpose: the other self proofs pick the
# newest build of that pattern automatically, and this directory holds only the
# objects of this family.
BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-nvn-buffer-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-31-buffer-contract-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -DNSO_WITH_CXX_GUARDS=1 -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

# Every run of this script gets its own directory, so that the raw stdout and
# stderr of one run never overwrite the raw output of another one. The
# directory is named after the time of the run, and the summary log keeps the
# path the card names.
RUN_ID="$(date '+%Y%m%d-%H%M%S')-$$"
RUN_OUT="${OUT}/run-${RUN_ID}"
mkdir -p "${BUILD}" "${RUN_OUT}" "$(dirname "${LOG}")" || exit 98
# The log of this run is kept next to its raw output as well, so that the
# output of an earlier run is still there.
RUN_LOG="${RUN_OUT}/selfproof.log"
cp "${LOG}" "${RUN_LOG}.previous" 2>/dev/null || true
: > "${LOG}"
: > "${RUN_LOG}"
{
    echo "NVN buffer contract self-proof: a broken expectation and a buffer family that lies"
    echo "  baseline  = the contract as it is, with the real family        -> must be GREEN"
    echo "  variant A = broken expectation, gate kept                      -> must be RED"
    echo "  variant B = broken expectation, gate removed, count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the family does not copy the builder's description -> RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
    echo "run directory: ${RUN_OUT}"
} | tee -a "${LOG}" "${RUN_LOG}"

python3 scripts/runtime/make_nvn_buffer_variants.py >> "${LOG}" 2>&1 || {
    echo "could not build the variants" >&2
    exit 97
}
# The objects of the runtime, compiled from the sources of this checkout: the
# family is new, so the newest build directory does not have it yet.
OBJECTS="nso_loader=src/runtime/nso/nso_loader.c
guest_context=src/runtime/hle/guest_context.c
guest_process=src/runtime/hle/guest_process.c
nso_host=src/runtime/hle/nso_host.c
address_region=src/runtime/hle/address_region.c
mutex=src/runtime/hle/mutex.c
exit_registry=src/runtime/hle/exit_registry.c
module_tls=src/runtime/hle/module_tls.c
guest_tp=src/runtime/hle/guest_tp.c
guest_pages=src/runtime/hle/guest_pages.c
native_boundary=src/runtime/nso/native_boundary.c
guest_os_tls=src/runtime/hle/guest_os_tls.c
native_window=src/runtime/hle/native_window.c
nvn_queue=src/graphics/nvn_device/nvn_queue.c
nvn_window=src/graphics/nvn_device/nvn_window.c
nvn_memory_pool=src/graphics/nvn_device/nvn_memory_pool.c
nvn_sampler_pool=src/graphics/nvn_device/nvn_sampler_pool.c
nvn_sampler_builder=src/graphics/nvn_device/nvn_sampler_builder.c
nvn_sampler=src/graphics/nvn_device/nvn_sampler.c
nvn_texture_pool=src/graphics/nvn_device/nvn_texture_pool.c
nvn_texture_builder=src/graphics/nvn_device/nvn_texture_builder.c
nvn_texture=src/graphics/nvn_device/nvn_texture.c
nvn_texture_view=src/graphics/nvn_device/nvn_texture_view.c
nvn_blend_state=src/graphics/nvn_device/nvn_blend_state.c
nvn_render_state=src/graphics/nvn_device/nvn_render_state.c
nvn_program=src/graphics/nvn_device/nvn_program.c
nvn_sha256=src/graphics/nvn_device/nvn_sha256.c
nvn_command_buffer=src/graphics/nvn_device/nvn_command_buffer.c
nvn_command_buffer.generated=src/graphics/nvn_device/nvn_command_buffer.generated.c
nvn_backend_none=src/graphics/nvn_device/nvn_backend_none.c"

LIBOBJS=""
for pair in ${OBJECTS}; do
    stem="${pair%%=*}"
    source="${pair#*=}"
    if [ ! -f "${source}" ]; then
        echo "missing source ${source}" >&2
        exit 95
    fi
    object="${BUILD}/${stem}.host.o"
    if [ ! -f "${object}" ] || [ "${source}" -nt "${object}" ]; then
        clang ${STRICT} -c "${source}" -o "${object}" >> "${LOG}" 2>&1 || {
            echo "could not compile ${source}" >&2
            exit 94
        }
    fi
    LIBOBJS="${LIBOBJS} ${object}"
done

# The variants of the contract and the stub of the family are copied into the
# directory of this run, so that the raw sources of one run do not overwrite
# the raw sources of another one. The copies live outside the tree, so the
# includes of the contract, which are relative to tests/native, are rewritten
# to the absolute path of this checkout.
for artefact in nvn_buffer_broken_fixed.c nvn_buffer_broken_prefix.c \
                nvn_buffer_stub_no_description_copy.c nvn_device_with_buffer_family.c; do
    sed "s|\"\.\./\.\./src/|\"${ROOT}/src/|g" "${OUT}/${artefact}" > "${RUN_OUT}/${artefact}" || exit 96
done

# The buffer family itself, and the device layer with the family wired in.
clang ${STRICT} -c src/graphics/nvn_device/nvn_buffer.c -o "${BUILD}/nvn_buffer.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the buffer family" >&2
    exit 93
}
clang ${STRICT} -c "${RUN_OUT}/nvn_device_with_buffer_family.c" -o "${BUILD}/nvn_device.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the device layer with the buffer family" >&2
    exit 93
}
LIBOBJS="${LIBOBJS} ${BUILD}/nvn_device.host.o"

# Variant C needs its own buffer object: the one that does not copy the
# builder's description into the buffer.
clang ${STRICT} -c "${RUN_OUT}/nvn_buffer_stub_no_description_copy.c" \
    -o "${RUN_OUT}/nvn_buffer_stub_no_description_copy.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the buffer stub" >&2
    exit 93
}

run_variant () {
    local name="$1" src="$2" objs="$3" expected="$4"
    # A source that is in the tree is used where it is; a source that the
    # variant generator wrote is used from the directory of this run.
    local source="${src}"
    if [ ! -f "${source}" ] && [ -f "${RUN_OUT}/${src}" ]; then
        source="${RUN_OUT}/${src}"
    fi
    clang ${STRICT} "${source}" ${objs} -lc++ -lpthread -o "${RUN_OUT}/${name}.host" >> "${LOG}" 2>&1 || {
        echo "could not link ${name}" >&2
        exit 92
    }
    "${RUN_OUT}/${name}.host" --nvn-buffer > "${RUN_OUT}/${name}.stdout" 2> "${RUN_OUT}/${name}.stderr"
    local rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|buffer contract line=" "${RUN_OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${RUN_OUT}/${name}.stdout")"
    } >> "${LOG}"
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|buffer contract line=" "${RUN_OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${RUN_OUT}/${name}.stdout")"
    } >> "${RUN_LOG}"
    if [ "${rc}" != "${expected}" ]; then
        echo "variant ${name}: exit ${rc}, expected ${expected}" >&2
        return 1
    fi
    return 0
}

status=0
run_variant nvn_buffer_baseline tests/native/nvn_buffer_contract.c \
    "${LIBOBJS} ${BUILD}/nvn_buffer.host.o" 0 || status=1
run_variant nvn_buffer_broken_fixed "nvn_buffer_broken_fixed.c" \
    "${LIBOBJS} ${BUILD}/nvn_buffer.host.o" 1 || status=1
run_variant nvn_buffer_broken_prefix "nvn_buffer_broken_prefix.c" \
    "${LIBOBJS} ${BUILD}/nvn_buffer.host.o" 0 || status=1
run_variant nvn_buffer_stub_no_description_copy tests/native/nvn_buffer_contract.c \
    "${LIBOBJS} ${OUT}/nvn_buffer_stub_no_description_copy.host.o" 1 || status=1

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
exit "${status}"
