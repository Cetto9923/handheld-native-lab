#!/usr/bin/env bash
# The NVN sampler contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims a wrap mode the guest never
#       set), the final gate kept                                  -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                            -> the false green
#   C = the contract untouched, the sampler family stops copying the builder's
#       parameters into the sampler                               -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction: a
# sampler is a snapshot of the builder it was made from, so a family that does
# not copy the parameters would answer with values the guest never set.
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
BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-nvn-sampler-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-25-sampler-contract-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -DNSO_WITH_CXX_GUARDS=1 -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

mkdir -p "${BUILD}" "${OUT}" "$(dirname "${LOG}")" || exit 98
: > "${LOG}"
{
    echo "NVN sampler contract self-proof: a broken expectation and a sampler family that lies"
    echo "  baseline  = the contract as it is, with the real family        -> must be GREEN"
    echo "  variant A = broken expectation, gate kept                      -> must be RED"
    echo "  variant B = broken expectation, gate removed, count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the family does not copy the builder's parameters -> RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
} >> "${LOG}"

python3 scripts/runtime/make_nvn_sampler_variants.py >> "${LOG}" 2>&1 || {
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

# The sampler family itself, and the device layer with the family wired in.
clang ${STRICT} -c src/graphics/nvn_device/nvn_sampler.c -o "${BUILD}/nvn_sampler.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the sampler family" >&2
    exit 93
}
clang ${STRICT} -c "${OUT}/nvn_device_with_sampler_family.c" -o "${BUILD}/nvn_device.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the device layer with the sampler family" >&2
    exit 93
}
LIBOBJS="${LIBOBJS} ${BUILD}/nvn_device.host.o"

# Variant C needs its own sampler object: the one that does not copy the
# builder's parameters into the sampler.
clang ${STRICT} -c "${OUT}/nvn_sampler_stub_no_parameter_copy.c" \
    -o "${OUT}/nvn_sampler_stub_no_parameter_copy.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the sampler stub" >&2
    exit 93
}

run_variant () {
    local name="$1" src="$2" objs="$3" expected="$4"
    clang ${STRICT} "${src}" ${objs} -lc++ -lpthread -o "${OUT}/${name}.host" >> "${LOG}" 2>&1 || {
        echo "could not link ${name}" >&2
        exit 92
    }
    "${OUT}/${name}.host" --nvn-sampler > "${OUT}/${name}.stdout" 2> "${OUT}/${name}.stderr"
    local rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|sampler contract line=" "${OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${OUT}/${name}.stdout")"
    } >> "${LOG}"
    if [ "${rc}" != "${expected}" ]; then
        echo "variant ${name}: exit ${rc}, expected ${expected}" >&2
        return 1
    fi
    return 0
}

status=0
run_variant nvn_sampler_baseline tests/native/nvn_sampler_contract.c \
    "${LIBOBJS} ${BUILD}/nvn_sampler.host.o" 0 || status=1
run_variant nvn_sampler_broken_fixed "${OUT}/nvn_sampler_broken_fixed.c" \
    "${LIBOBJS} ${BUILD}/nvn_sampler.host.o" 1 || status=1
run_variant nvn_sampler_broken_prefix "${OUT}/nvn_sampler_broken_prefix.c" \
    "${LIBOBJS} ${BUILD}/nvn_sampler.host.o" 0 || status=1
run_variant nvn_sampler_stub_no_parameter_copy tests/native/nvn_sampler_contract.c \
    "${LIBOBJS} ${OUT}/nvn_sampler_stub_no_parameter_copy.host.o" 1 || status=1

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
exit "${status}"
