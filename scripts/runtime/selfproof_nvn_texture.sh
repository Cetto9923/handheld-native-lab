#!/usr/bin/env bash
# The NVN texture contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims a width the guest never set),
#       the final gate kept                                  -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                            -> the false green
#   C = the contract untouched, the texture family stops copying the description
#       of the builder into the texture                       -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction: a
# texture is a snapshot of the builder it was made from, so a family that does
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
BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-nvn-texture-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-26-texture-contract-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -DNSO_WITH_CXX_GUARDS=1 -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

mkdir -p "${BUILD}" "${OUT}" "$(dirname "${LOG}")" || exit 98
: > "${LOG}"
{
    echo "NVN texture contract self-proof: a broken expectation and a texture family that lies"
    echo "  baseline  = the contract as it is, with the real family        -> must be GREEN"
    echo "  variant A = broken expectation, gate kept                      -> must be RED"
    echo "  variant B = broken expectation, gate removed, count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the family does not copy the description of the builder -> RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
} >> "${LOG}"

python3 scripts/runtime/make_nvn_texture_variants.py >> "${LOG}" 2>&1 || {
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

# The two families themselves, and the device layer with the families wired in.
clang ${STRICT} -c src/graphics/nvn_device/nvn_texture.c -o "${BUILD}/nvn_texture.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the texture family" >&2
    exit 93
}
clang ${STRICT} -c src/graphics/nvn_device/nvn_texture_view.c -o "${BUILD}/nvn_texture_view.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the texture view family" >&2
    exit 93
}
clang ${STRICT} -c "${OUT}/nvn_device_with_texture_family.c" -o "${BUILD}/nvn_device.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the device layer with the texture family" >&2
    exit 93
}
LIBOBJS="${LIBOBJS} ${BUILD}/nvn_texture_view.host.o ${BUILD}/nvn_device.host.o"
# The texture object is added per variant, because variant C runs against the
# stub instead of the real family.
TEXTURE_OBJECT="${BUILD}/nvn_texture.host.o"

# Variant C needs its own texture object: the one that does not copy the
# description of the builder into the texture.
clang ${STRICT} -c "${OUT}/nvn_texture_stub_no_snapshot.c" \
    -o "${OUT}/nvn_texture_stub_no_snapshot.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the texture stub" >&2
    exit 93
}

status=0

# Run one variant and check the exit code, the report on stdout and the truth
# on stderr. The name is what the log and the report of this self proof use.
run_variant () {
    local name="$1" src="$2" objs="$3" expected="$4"
    local rc=0
    local stdout_file="${OUT}/${name}.stdout"
    local stderr_file="${OUT}/${name}.stderr"
    clang ${STRICT} "${src}" ${objs} -lc++ -lpthread -o "${OUT}/${name}.host" >> "${LOG}" 2>&1 || {
        echo "could not link ${name}" >&2
        status=1
        return 1
    }
    "${OUT}/${name}.host" --nvn-texture > "${stdout_file}" 2> "${stderr_file}"
    rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|texture contract line=" "${stderr_file}" | head -8
        echo "stdout: $(cat "${stdout_file}")"
    } >> "${LOG}"
    if [ "${rc}" != "${expected}" ]; then
        echo "variant ${name}: exit ${rc}, expected ${expected}" >&2
        status=1
        return 1
    fi
    return 0
}

# The baseline has to be green and has to say that there was no error.
run_variant nvn_texture_baseline tests/native/nvn_texture_contract.c \
    "${LIBOBJS} ${TEXTURE_OBJECT}" 0 || status=1
if ! grep -q '"nvn_texture_errors":0' "${OUT}/nvn_texture_baseline.stdout"; then
    echo "the baseline does not report zero errors" >&2
    status=1
fi
if grep -q "\[check-fail\]" "${OUT}/nvn_texture_baseline.stderr"; then
    echo "the baseline has a failed expectation" >&2
    status=1
fi

# Variant A: the broken expectation has to be caught by the final gate.
run_variant nvn_texture_broken_fixed "${OUT}/nvn_texture_broken_fixed.c" \
    "${LIBOBJS} ${TEXTURE_OBJECT}" 1 || status=1
if ! grep -q "\[check-fail\]" "${OUT}/nvn_texture_broken_fixed.stderr"; then
    echo "variant A did not report the broken expectation" >&2
    status=1
fi

# Variant B: the false green. The exit code is 0 and the report says that
# there was no error, while the truth is on stderr.
run_variant nvn_texture_broken_prefix "${OUT}/nvn_texture_broken_prefix.c" \
    "${LIBOBJS} ${TEXTURE_OBJECT}" 0 || status=1
if ! grep -q '"nvn_texture_errors":0' "${OUT}/nvn_texture_broken_prefix.stdout"; then
    echo "variant B does not report the zero errors it hardcoded" >&2
    status=1
fi
if ! grep -q "\[check-fail\]" "${OUT}/nvn_texture_broken_prefix.stderr"; then
    echo "variant B lost the true failure on stderr" >&2
    status=1
fi

# Variant C: the family that does not copy the description has to be caught.
run_variant nvn_texture_stub_no_snapshot tests/native/nvn_texture_contract.c \
    "${LIBOBJS} ${OUT}/nvn_texture_stub_no_snapshot.host.o" 1 || status=1

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
exit "${status}"
