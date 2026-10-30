#!/usr/bin/env bash
# The NVN queue submit contract self-proof. Four runs, one deliberate break
# each, and the script judges the exit codes itself:
#
#   baseline = nothing changed               -> exit 0, stdout errors:0
#   A        = a broken expectation          -> exit 1, stderr has a real [check-fail]
#   B        = the same break, the final gate removed and the reported error
#              count hardcoded to 0           -> exit 0, stdout still says errors:0
#                                                while stderr keeps the real
#                                                [check-fail]: the false green
#   C        = the contract untouched, the stub does not call the replay any
#              more (the seam is not joined)  -> exit 1
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction:
# it is the check that keeps the queue layer from dropping the seam, which is
# the failure mode that would look like a working port and be nothing on the
# GPU.
#
# The library is compiled here, from the sources of this checkout, so that the
# proof is of the tree that is being worked on and not of an older build.
# Every run gets a directory of its own; no log is overwritten.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

STAMP="$(date '+%Y%m%d-%H%M%S')"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-32-queue-submit-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}/p-mm-32-${STAMP}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -I . -I src/runtime/hle -I src/runtime/nso -I src/graphics/nvn_device"

mkdir -p "${OUT}" "$(dirname "${LOG}")"

# The sources of the library, the ones the contract needs to be linked with.
SOURCES="
src/runtime/nso/nso_loader.c
src/runtime/nso/native_boundary.c
src/runtime/hle/guest_context.c
src/runtime/hle/guest_process.c
src/runtime/hle/nso_host.c
src/runtime/hle/address_region.c
src/runtime/hle/mutex.c
src/runtime/hle/exit_registry.c
src/runtime/hle/module_tls.c
src/runtime/hle/guest_tp.c
src/runtime/hle/guest_pages.c
src/runtime/hle/guest_os_tls.c
src/runtime/hle/native_window.c
src/graphics/nvn_device/nvn_device.c
src/graphics/nvn_device/nvn_queue.c
src/graphics/nvn_device/nvn_window.c
src/graphics/nvn_device/nvn_memory_pool.c
src/graphics/nvn_device/nvn_sampler_pool.c
src/graphics/nvn_device/nvn_sampler_builder.c
src/graphics/nvn_device/nvn_sampler.c
src/graphics/nvn_device/nvn_texture_pool.c
src/graphics/nvn_device/nvn_texture_builder.c
src/graphics/nvn_device/nvn_texture.c
src/graphics/nvn_device/nvn_texture_view.c
src/graphics/nvn_device/nvn_blend_state.c
src/graphics/nvn_device/nvn_render_state.c
src/graphics/nvn_device/nvn_program.c
src/graphics/nvn_device/nvn_sha256.c
src/graphics/nvn_device/nvn_command_buffer.c
src/graphics/nvn_device/nvn_command_buffer.generated.c
src/graphics/nvn_device/nvn_command_replay.c
src/graphics/nvn_device/nvn_backend_none.c
"

{
    echo "NVN queue submit contract self-proof: the seam, and a queue layer that lies"
    echo "  baseline = nothing changed                                    -> exit 0"
    echo "  A = broken expectation, gate kept                             -> exit 1"
    echo "  B = broken expectation, gate removed and count hardcoded 0    -> exit 0 (false green)"
    echo "  C = contract untouched, the stub does not call the replay      -> exit 1"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "run directory: ${OUT}"
} > "${LOG}"

# The library, without the queue layer: variant C brings its own queue layer.
LIBOBJS=""
for source in ${SOURCES}; do
    case "${source}" in
        src/graphics/nvn_device/nvn_queue.c) continue ;;
    esac
    object="${OUT}/$(basename "${source%.c}").host.o"
    clang ${STRICT} -DNSO_WITH_CXX_GUARDS=1 -c "${source}" -o "${object}" >> "${LOG}" 2>&1 || {
        echo "could not compile ${source}" >&2
        exit 97
    }
    LIBOBJS="${LIBOBJS} ${object}"
done

# The queue layer of the tree, and the broken one of variant C.
clang ${STRICT} -DNSO_WITH_CXX_GUARDS=1 -c src/graphics/nvn_device/nvn_queue.c \
    -o "${OUT}/nvn_queue.host.o" >> "${LOG}" 2>&1 || { echo "could not compile the queue layer" >&2; exit 97; }
python3 scripts/runtime/make_nvn_queue_submit_variants.py "${OUT}" >> "${LOG}" 2>&1 || {
    echo "could not build the variants" >&2
    exit 97
}
clang ${STRICT} -DNSO_WITH_CXX_GUARDS=1 -c "${OUT}/nvn_queue_submit_stub_without_the_replay.c" \
    -o "${OUT}/nvn_queue_submit_stub_without_the_replay.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the broken stub" >&2
    exit 97
}

# run_variant <name> <contract source> <queue object>
# The exit code, the stdout and the number of real [check-fail] lines are
# kept in arrays, so that the verdicts below do not have to read the log
# back.
VARIANT_NAMES=()
VARIANT_EXIT=()
VARIANT_STDOUT=()
VARIANT_CHECK_FAIL=()

run_variant () {
    local name="$1" contract="$2" queue_object="$3"
    clang ${STRICT} "${contract}" ${LIBOBJS} "${queue_object}" -lc++ -lpthread \
        -o "${OUT}/${name}.host" >> "${LOG}" 2>&1 || {
        echo "could not build ${name}" >&2
        exit 97
    }
    "${OUT}/${name}.host" --nvn-queue-submit > "${OUT}/${name}.stdout" 2> "${OUT}/${name}.stderr"
    local rc=$?
    local check_fail
    check_fail="$(grep -c "\[check-fail\]" "${OUT}/${name}.stderr")"
    VARIANT_NAMES+=("${name}")
    VARIANT_EXIT+=("${rc}")
    VARIANT_STDOUT+=("$(cat "${OUT}/${name}.stdout")")
    VARIANT_CHECK_FAIL+=("${check_fail}")
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc}"
        echo "stdout: $(cat "${OUT}/${name}.stdout")"
        echo "check_fail_lines: ${check_fail}"
        grep -E "\[check-fail\]" "${OUT}/${name}.stderr" | head -8
    } >> "${LOG}"
    return 0
}

run_variant nvn_queue_submit_baseline \
    tests/native/nvn_queue_submit_contract.c "${OUT}/nvn_queue.host.o"
run_variant nvn_queue_submit_broken_fixed \
    "${OUT}/nvn_queue_submit_broken_fixed.c" "${OUT}/nvn_queue.host.o"
run_variant nvn_queue_submit_broken_prefix \
    "${OUT}/nvn_queue_submit_broken_prefix.c" "${OUT}/nvn_queue.host.o"
run_variant nvn_queue_submit_stub_without_the_replay \
    tests/native/nvn_queue_submit_contract.c "${OUT}/nvn_queue_submit_stub_without_the_replay.host.o"

# The four verdicts. The script judges them itself: a run that is not what it
# has to be is a failure of the proof, not a note in it.
#
#   name                                   exit  stdout        stderr
#   baseline                               0     errors:0      quiet
#   A (broken expectation, gate kept)      1     errors>0      [check-fail]
#   B (gate removed, count hardcoded 0)    0     errors:0      [check-fail]  <- the false green
#   C (the stub does not call the replay)  1     errors>0      [check-fail]
FAILURES=0
verdict () {
    local name="$1" want_rc="$2" want_errors_zero="$3" want_check_fail="$4"
    local index=-1 i rc stdout check_fail errors_zero verdict
    for i in "${!VARIANT_NAMES[@]}"; do
        if [ "${VARIANT_NAMES[$i]}" = "${name}" ]; then index=$i; fi
    done
    if [ "${index}" -lt 0 ]; then
        echo "verdict ${name}: NOT AS EXPECTED (the variant did not run)" | tee -a "${LOG}"
        FAILURES=$((FAILURES + 1))
        return 0
    fi
    rc="${VARIANT_EXIT[$index]}"
    stdout="${VARIANT_STDOUT[$index]}"
    check_fail="${VARIANT_CHECK_FAIL[$index]}"
    errors_zero=0
    echo "${stdout}" | grep -q '"nvn_queue_submit_errors":0,' && errors_zero=1

    verdict="as expected"
    if [ "${rc}" != "${want_rc}" ]; then
        verdict="NOT AS EXPECTED"
    elif [ "${want_errors_zero}" = "yes" ] && [ "${errors_zero}" != "1" ]; then
        verdict="NOT AS EXPECTED"
    elif [ "${want_errors_zero}" = "no" ] && [ "${errors_zero}" != "0" ]; then
        verdict="NOT AS EXPECTED"
    elif [ "${want_check_fail}" = "yes" ] && [ "${check_fail}" -lt 1 ]; then
        verdict="NOT AS EXPECTED"
    elif [ "${want_check_fail}" = "no" ] && [ "${check_fail}" -ne 0 ]; then
        verdict="NOT AS EXPECTED"
    fi
    if [ "${verdict}" != "as expected" ]; then
        FAILURES=$((FAILURES + 1))
    fi
    local line="verdict ${name}: ${verdict} (exit=${rc} wanted=${want_rc}, stdout_errors_zero=${errors_zero} wanted=${want_errors_zero}, check_fail_lines=${check_fail} wanted=${want_check_fail})"
    echo "${line}" | tee -a "${LOG}"
}

# The baseline: green, and it says so.
verdict nvn_queue_submit_baseline 0 yes no
# A: red, with a real [check-fail] on the stderr, and the count is not zero.
verdict nvn_queue_submit_broken_fixed 1 no yes
# B: green, and it lies: the stdout says errors:0 while the stderr holds the
# real [check-fail]. This is the false green, and it has to be measured.
verdict nvn_queue_submit_broken_prefix 0 yes yes
# C: red. The seam is not joined and the contract says so.
verdict nvn_queue_submit_stub_without_the_replay 1 no yes

{
    echo ""
    echo "failures: ${FAILURES}"
} >> "${LOG}"

echo "self-proof written to ${LOG}"
grep -E "^verdict |^exit_code=|^stdout:|^check_fail_lines:|^failures:" "${LOG}"

if [ "${FAILURES}" -ne 0 ]; then
    echo "the self-proof did not come out as it had to" >&2
    exit 1
fi
exit 0
