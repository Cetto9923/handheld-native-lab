#!/usr/bin/env bash
# The NVN event contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims a value the signal never
#       carried), the final gate kept                      -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                    -> the false green
#   C = the contract untouched, the event family stops recording the value of
#       a signal                                          -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction: a
# signal is the point of the family, so a family that drops the value would
# answer with a value the guest never gave.
#
# Variant B is measured, not just run: the script checks that the process
# exits 0 AND that its stdout still claims zero errors AND that its stderr
# still holds the real failure. A false green that is only noticed by reading
# the log is a false green that nobody notices.
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
BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-nvn-event-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-46-event-contract-selfproof.log}"
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
    echo "NVN event contract self-proof: a broken expectation and an event family that lies"
    echo "  baseline  = the contract as it is, with the real family        -> must be GREEN"
    echo "  variant A = broken expectation, gate kept                      -> must be RED"
    echo "  variant B = broken expectation, gate removed, count hardcoded 0 -> false green"
    echo "  variant C = contract untouched, the family drops the signalled value -> RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
    echo "run directory: ${RUN_OUT}"
} | tee -a "${LOG}" "${RUN_LOG}"

python3 scripts/runtime/make_nvn_event_variants.py >> "${LOG}" 2>&1 || {
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
nvn_command_replay=src/graphics/nvn_device/nvn_command_replay.c
nvn_present=src/graphics/nvn_device/nvn_present.c
nvn_buffer=src/graphics/nvn_device/nvn_buffer.c
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
for artefact in nvn_event_broken_fixed.c nvn_event_broken_prefix.c \
                nvn_event_stub_no_signal_value.c nvn_device_with_event_family.c; do
    sed "s|\"\.\./\.\./src/|\"${ROOT}/src/|g" "${OUT}/${artefact}" > "${RUN_OUT}/${artefact}" || exit 96
done

# The event family itself, and the device layer with the family wired in.
clang ${STRICT} -c src/graphics/nvn_device/nvn_event.c -o "${BUILD}/nvn_event.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the event family" >&2
    exit 93
}
clang ${STRICT} -c "${RUN_OUT}/nvn_device_with_event_family.c" -o "${BUILD}/nvn_device.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the device layer with the event family" >&2
    exit 93
}
LIBOBJS="${LIBOBJS} ${BUILD}/nvn_device.host.o"

# Variant C needs its own event object: the one that does not record the
# value of a signal.
clang ${STRICT} -c "${RUN_OUT}/nvn_event_stub_no_signal_value.c" \
    -o "${RUN_OUT}/nvn_event_stub_no_signal_value.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the event stub" >&2
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
    "${RUN_OUT}/${name}.host" --nvn-event > "${RUN_OUT}/${name}.stdout" 2> "${RUN_OUT}/${name}.stderr"
    local rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|event contract line=" "${RUN_OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${RUN_OUT}/${name}.stdout")"
    } >> "${LOG}"
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|event contract line=" "${RUN_OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${RUN_OUT}/${name}.stdout")"
    } >> "${RUN_LOG}"
    if [ "${rc}" != "${expected}" ]; then
        echo "variant ${name}: exit ${rc}, expected ${expected}" >&2
        return 1
    fi
    return 0
}

status=0
run_variant nvn_event_baseline tests/native/nvn_event_contract.c \
    "${LIBOBJS} ${BUILD}/nvn_event.host.o" 0 || status=1
run_variant nvn_event_broken_fixed "nvn_event_broken_fixed.c" \
    "${LIBOBJS} ${BUILD}/nvn_event.host.o" 1 || status=1
run_variant nvn_event_broken_prefix "nvn_event_broken_prefix.c" \
    "${LIBOBJS} ${BUILD}/nvn_event.host.o" 0 || status=1
run_variant nvn_event_stub_no_signal_value tests/native/nvn_event_contract.c \
    "${LIBOBJS} ${RUN_OUT}/nvn_event_stub_no_signal_value.host.o" 1 || status=1

# The false green of variant B is measured, not just run. The process exits 0,
# but its stdout still claims that there was no error, and its stderr still
# holds the failure that the removed gate was hiding. A contract that could
# only be caught by reading a log is a contract that a pipeline does not catch.
echo ""
echo "== the false green of variant B, measured ==" >> "${LOG}"
B_STDOUT="${RUN_OUT}/nvn_event_broken_prefix.stdout"
B_STDERR="${RUN_OUT}/nvn_event_broken_prefix.stderr"
B_LIES=0
grep -q '"nvn_event_errors":0' "${B_STDOUT}" 2>/dev/null || B_LIES=1
grep -q '\[check-fail\]' "${B_STDERR}" 2>/dev/null || B_LIES=1
if [ "${B_LIES}" -ne 0 ]; then
    echo "variant B did not lie the way it has to: the stdout does not claim zero errors, or the stderr does not hold the real failure" >&2
    status=1
fi
{
    echo "stdout_claims_zero_errors=$(grep -c '"nvn_event_errors":0' "${B_STDOUT}" 2>/dev/null || echo 0)"
    echo "stderr_holds_the_real_failure=$(grep -c '\[check-fail\]' "${B_STDERR}" 2>/dev/null || echo 0)"
    echo "the false green is measured: exit 0 with a lie on stdout and the truth on stderr"
} | tee -a "${LOG}" "${RUN_LOG}"

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]|the false green" "${LOG}"
exit "${status}"
