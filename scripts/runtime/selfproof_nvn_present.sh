#!/usr/bin/env bash
# The NVN present contract self-proof. Three variants, one deliberate break each:
#   A = broken expectation (the contract claims that a frame was shown when the
#       step failed), the final gate kept                          -> must be RED
#   B = broken expectation, the final gate removed and the reported error count
#       hardcoded to 0                                            -> the false green
#   C = the contract untouched, the fake backend claims that it presented a
#       frame it never presented                                 -> must be RED
#
# A and B differ only in the final gate, so a single variant run cannot tell
# "fixed" from "the assertion was always true". C closes the other direction: the
# present path may only report a frame that was really shown, so a backend that
# reports one without showing anything has to be caught.
#
# The baseline run at the end is the unmutated contract with the real module:
# it has to be green, otherwise the three variants prove nothing.
#
# The module of the present path is part of the host build of the runtime now,
# so the contract is linked against the module of the tree instead of bringing
# its own copy of it. The file in the tree is not touched.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

# The build directory of this self proof. It does not match the
# .local/build-p-mm-* pattern on purpose: the other self proofs pick the
# newest build of that pattern automatically, and this directory holds only the
# objects of this family.
BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-nvn-present-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/p-mm-28-present-selfproof.log}"
OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}"
STRICT="-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -I src/graphics/nvn_device"

mkdir -p "${BUILD}" "${OUT}" "$(dirname "${LOG}")" || exit 98
: > "${LOG}"
{
    echo "NVN present contract self-proof: a broken expectation and a backend that lies"
    echo "  baseline  = the contract as it is, with the real module          -> must be GREEN"
    echo "  variant A = broken expectation, gate kept                        -> must be RED"
    echo "  variant B = broken expectation, gate removed, count hardcoded 0  -> false green"
    echo "  variant C = contract untouched, the fake backend claims a frame  -> must be RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
} >> "${LOG}"

python3 scripts/runtime/make_nvn_present_variants.py >> "${LOG}" 2>&1 || {
    echo "could not build the variants" >&2
    exit 97
}

# The backend of a host build. It has no Vulkan implementation, so it refuses
# every step of the present path and says why.
clang ${STRICT} -c src/graphics/nvn_device/nvn_backend_none.c \
    -o "${BUILD}/nvn_backend_none.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the backend of the host build" >&2
    exit 94
}

# The present path itself, the module of the tree.
clang ${STRICT} -c src/graphics/nvn_device/nvn_present.c \
    -o "${BUILD}/nvn_present.host.o" >> "${LOG}" 2>&1 || {
    echo "could not compile the present path" >&2
    exit 93
}

run_variant () {
    local name="$1" src="$2" expected="$3"
    clang ${STRICT} "${src}" "${BUILD}/nvn_present.host.o" "${BUILD}/nvn_backend_none.host.o" -lpthread \
        -o "${OUT}/${name}.host" >> "${LOG}" 2>&1 || {
        echo "could not link ${name}" >&2
        exit 92
    }
    "${OUT}/${name}.host" --nvn-present > "${OUT}/${name}.stdout" 2> "${OUT}/${name}.stderr"
    local rc=$?
    {
        echo ""
        echo "== variant: ${name} =="
        echo "exit_code=${rc} expected=${expected}"
        grep -E "\[check-fail\]|present contract line=" "${OUT}/${name}.stderr" | head -8
        echo "stdout: $(cat "${OUT}/${name}.stdout")"
    } >> "${LOG}"
    if [ "${rc}" != "${expected}" ]; then
        echo "variant ${name}: exit ${rc}, expected ${expected}" >&2
        return 1
    fi
    return 0
}

status=0

# The baseline: the contract as it is. It has to be green, otherwise the
# three variants below prove nothing.
run_variant nvn_present_baseline tests/native/nvn_present_contract.c 0 || status=1
if [ "${status}" -eq 0 ]; then
    if ! grep -q '"nvn_present_errors":0' "${OUT}/nvn_present_baseline.stdout"; then
        echo "the baseline does not report zero errors" >&2
        status=1
    fi
fi

# A: the broken expectation with the final gate. It has to be red, and the
# reason has to be in the log.
run_variant nvn_present_broken_fixed "${OUT}/nvn_present_broken_fixed.c" 1 || status=1
if ! grep -q "\[check-fail\]" "${OUT}/nvn_present_broken_fixed.stderr"; then
    echo "variant A is red but the log has no real check failure" >&2
    status=1
fi

# B: the same break with the final gate removed and the reported error count
# hardcoded to 0. It has to be green while the log still carries the truth,
# and the script measures that false green instead of only looking at the
# exit code.
run_variant nvn_present_broken_prefix "${OUT}/nvn_present_broken_prefix.c" 0 || status=1
if ! grep -q '"nvn_present_errors":0' "${OUT}/nvn_present_broken_prefix.stdout"; then
    echo "variant B does not lie about the error count" >&2
    status=1
fi
if ! grep -q "\[check-fail\]" "${OUT}/nvn_present_broken_prefix.stderr"; then
    echo "variant B is green but the log has no real check failure" >&2
    status=1
fi

# C: the contract untouched and the fake backend claims that it presented a
# frame. It has to be red, because a frame that was not shown may not be
# reported as shown.
run_variant nvn_present_stub_lies "${OUT}/nvn_present_stub_lies.c" 1 || status=1
if ! grep -q "\[check-fail\]" "${OUT}/nvn_present_stub_lies.stderr"; then
    echo "variant C is red but the log has no real check failure" >&2
    status=1
fi

echo "self-proof written to ${LOG}"
grep -E "exit_code=|stdout:|\[check-fail\]" "${LOG}"
exit "${status}"
