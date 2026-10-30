#!/usr/bin/env bash
# The NVN builder contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims a different default wrap mode
#       than the reference host uses), the final gate kept   -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                      -> shows the false green
#   C = the contract untouched, the builder layer stops applying the defaults
#                                                            -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction:
# the defaults of a builder have to be the ones the reference host that runs
# this game uses, so a layer that does not apply them would hand the guest a
# sampler or a texture that does not match what the game expects.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

BUILD="${FFRS_SELF_PROOF_BUILD:-}"
# The contract file carries every NVN family, so the self-proof needs the
# newest build and not the one of the round the family was written in.
if [ -z "${BUILD}" ]; then
    BUILD="$(ls -d .local/build-p-mm-* 2>/dev/null | sort -V | tail -1)"
fi
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-23-builder-contract-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

if [ ! -d "${BUILD}" ]; then
    echo "missing ${BUILD}; run scripts/runtime/build_guest_process_contract.sh first" >&2
    exit 98
fi

mkdir -p "${OUT}"
: > "${LOG}"
{
    echo "NVN builder contract self-proof: a broken expectation and a builder layer that lies"
    echo "  variant A = broken expectation, gate kept -> must be RED"
    echo "  variant B = broken expectation, gate removed and count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the layer does not apply the defaults -> must be RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
} >> "${LOG}"

python3 scripts/runtime/make_nvn_builder_variants.py >> "${LOG}" 2>&1 || {
    echo "could not build the variants" >&2
    exit 97
}

LIBOBJS=""
for object in nso_loader guest_context guest_process nso_host address_region mutex \
             exit_registry module_tls guest_tp guest_pages native_boundary guest_os_tls \
             native_window nvn_device nvn_queue nvn_window nvn_memory_pool nvn_sampler_pool \
             nvn_texture_pool nvn_blend_state nvn_render_state nvn_program nvn_sha256 \
             nvn_command_buffer nvn_command_buffer.generated nvn_texture_builder; do
    LIBOBJS="${LIBOBJS} ${BUILD}/${object}.host.o"
done

# Variant C needs its own sampler builder object: the one that does not apply
# the defaults.
clang ${STRICT} -c "${OUT}/nvn_sampler_builder_stub_no_defaults.c" \
    -o "${OUT}/nvn_sampler_builder_stub_no_defaults.host.o" >> "${LOG}" 2>&1

run_variant () {
    local name="$1" src="$2" objs="$3"
    clang ${STRICT} "${src}" ${objs} -lc++ -lpthread -o "${OUT}/${name}.host" >> "${LOG}" 2>&1
    "${OUT}/${name}.host" --nvn-builder > "${OUT}/${name}.stdout" 2> "${OUT}/${name}.stderr"
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

run_variant nvn_builder_broken_fixed "${OUT}/nvn_builder_broken_fixed.c" \
    "${LIBOBJS} ${BUILD}/nvn_sampler_builder.host.o ${BUILD}/nvn_backend_none.host.o"
run_variant nvn_builder_broken_prefix "${OUT}/nvn_builder_broken_prefix.c" \
    "${LIBOBJS} ${BUILD}/nvn_sampler_builder.host.o ${BUILD}/nvn_backend_none.host.o"
run_variant nvn_builder_stub_no_defaults tests/native/guest_process_contract.c \
    "${LIBOBJS} ${OUT}/nvn_sampler_builder_stub_no_defaults.host.o ${BUILD}/nvn_backend_none.host.o"

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
