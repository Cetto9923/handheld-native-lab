#!/usr/bin/env bash
# The NVN memory pool contract self-proof. Three variants, one deliberate break
# each:
#   A = broken expectation (this build has to be able to create a pool), the
#       final gate kept                              -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                              -> shows the false green
#   C = the contract untouched, the memory pool layer hands out a pool even when
#       there is no real device behind it           -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction:
# it is the check that keeps the memory pool layer from handing out a pool that
# the graphics device cannot use, which is the failure mode the guest would hit
# as memory that never reaches the screen.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-p-mm-17}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-17-contract-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

if [ ! -d "${BUILD}" ]; then
    echo "missing ${BUILD}; run scripts/runtime/build_guest_process_contract.sh first" >&2
    exit 98
fi

mkdir -p "${OUT}"
: > "${LOG}"
{
    echo "NVN memory pool contract self-proof: a broken expectation and a pool layer that lies"
    echo "  variant A = broken expectation, gate kept -> must be RED"
    echo "  variant B = broken expectation, gate removed and count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the pool layer claims a pool without a device -> must be RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
} >> "${LOG}"

python3 scripts/runtime/make_nvn_memory_pool_variants.py >> "${LOG}" 2>&1 || {
    echo "could not build the variants" >&2
    exit 97
}

LIBOBJS=""
for object in nso_loader guest_context guest_process nso_host address_region mutex exit_registry \
              module_tls guest_tp guest_pages native_boundary guest_os_tls native_window nvn_device \
              nvn_queue nvn_window; do
    LIBOBJS="${LIBOBJS} ${BUILD}/${object}.host.o"
done

# Variant C needs its own memory pool object: the one that hands out a pool
# without a device.
clang ${STRICT} -c "${OUT}/nvn_memory_pool_stub_always_succeeds.c" \
    -o "${OUT}/nvn_memory_pool_stub_always_succeeds.host.o" >> "${LOG}" 2>&1

run_variant () {
    local name="$1" src="$2" objs="$3"
    clang ${STRICT} "${src}" ${objs} -lc++ -lpthread -o "${OUT}/${name}.host" >> "${LOG}" 2>&1
    "${OUT}/${name}.host" --nvn-memory-pool > "${OUT}/${name}.stdout" 2> "${OUT}/${name}.stderr"
    local rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc}"
        grep -E "\[check-fail\]|process contract line=" "${OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${OUT}/${name}.stdout")"
    } >> "${LOG}"
    return 0
}

run_variant nvn_memory_pool_broken_fixed "${OUT}/nvn_memory_pool_broken_fixed.c" \
    "${LIBOBJS} ${BUILD}/nvn_memory_pool.host.o ${BUILD}/nvn_backend_none.host.o"
run_variant nvn_memory_pool_broken_prefix "${OUT}/nvn_memory_pool_broken_prefix.c" \
    "${LIBOBJS} ${BUILD}/nvn_memory_pool.host.o ${BUILD}/nvn_backend_none.host.o"
run_variant nvn_memory_pool_stub_always_succeeds tests/native/guest_process_contract.c \
    "${LIBOBJS} ${OUT}/nvn_memory_pool_stub_always_succeeds.host.o ${BUILD}/nvn_backend_none.host.o"

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
