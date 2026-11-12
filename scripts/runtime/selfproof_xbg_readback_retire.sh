#!/usr/bin/env bash
# The self-proof of the retirement rule for a readback.
#
#   baseline  = the contract with the production header                        -> must be GREEN
#   mutated-1 = the contract with a header that waits for the NEWEST submission -> must be RED
#   mutated-2 = the contract with a header that ALWAYS waits                    -> must be RED
#
# Both mutations are rules the engine must not follow. Waiting for the newest submission in flight is the old
# over-wait in another shape: the readback's bytes belong to one submission, and the queue behind it is not the
# readback's business. Always waiting is the same over-wait without the check: a submission that already retired
# carries final bytes, and waiting for the rest of the queue buys nothing. A contract that also passes on a mutated
# header would prove nothing.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-xbg-readback-retire-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/xbg-readback-retire-selfproof.log}"
RUN_ID="$(date '+%Y%m%d-%H%M%S')-$$"
RUN_OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}/run-${RUN_ID}"
mkdir -p "${BUILD}" "${RUN_OUT}" "$(dirname "${LOG}")" || exit 98
LOG="${RUN_OUT}/selfproof.log"
: > "${LOG}"

STRICT="-std=c++17 -O2 -g -Wall -Wextra -Werror"
HEADER="src/graphics/nvn_vulkan/engine/xbg_readback_retire.h"
CONTRACT="tests/native/xbg_readback_retire_contract.cpp"

{
    echo "The self-proof of the readback retirement rule"
    echo "  baseline  = the contract with the production header                        -> must be GREEN"
    echo "  mutated-1 = the contract with a header that waits for the NEWEST submission -> must be RED"
    echo "  mutated-2 = the contract with a header that ALWAYS waits                    -> must be RED"
    echo "date: $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "head: $(git rev-parse HEAD)"
    echo "dirty: $(git status --short | tr '\n' ';')"
    echo "run directory: ${RUN_OUT}"
} | tee -a "${LOG}"

status=0

# The baseline: the production header with the contract.
clang++ ${STRICT} "${CONTRACT}" -o "${RUN_OUT}/contract" >> "${LOG}" 2>&1 || { echo "could not build the contract" >&2; exit 94; }
"${RUN_OUT}/contract" > "${RUN_OUT}/baseline.stdout" 2> "${RUN_OUT}/baseline.stderr"
baseline_rc=$?
echo "" >> "${LOG}"
echo "== baseline ==" >> "${LOG}"
echo "exit_code=${baseline_rc} expected=0" >> "${LOG}"
cat "${RUN_OUT}/baseline.stdout" >> "${LOG}"
grep -E "\[check-fail\]" "${RUN_OUT}/baseline.stderr" | head -8 >> "${LOG}"
if [ "${baseline_rc}" != "0" ]; then echo "baseline: exit ${baseline_rc}, expected 0" >&2; status=1; fi

# The two mutations.
mutate() {
    local name="$1" replacement="$2" out="$3"
    python3 - "${HEADER}" "${out}" "${replacement}" <<'PY'
import sys
src, out, replacement = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src).read()
lines = text.split("\n")
hits = [i for i, line in enumerate(lines) if line.lstrip().startswith("decision.wait=pending[owner];")]
if len(hits) != 1:
    print("the mutation anchor was not found exactly once:", hits, file=sys.stderr)
    raise SystemExit(3)
indent = lines[hits[0]][: len(lines[hits[0]]) - len(lines[hits[0]].lstrip())]
lines[hits[0]] = indent + replacement
open(out, "w").write("\n".join(lines))
PY
    if cmp -s "${HEADER}" "${out}"; then echo "the mutation did not change the header" >&2; return 3; fi
    return 0
}

run_mutation() {
    local name="$1" replacement="$2"
    local dir="${RUN_OUT}/mutated-${name}"
    mkdir -p "${dir}"
    mutate "${name}" "${replacement}" "${dir}/xbg_readback_retire.h" || return 3
    sed "s|"\.\./\.\./src/graphics/nvn_vulkan/engine/xbg_readback_retire.h"|"xbg_readback_retire.h"|" "${CONTRACT}" > "${dir}/contract.cpp" || return 3
    clang++ ${STRICT} "${dir}/contract.cpp" -o "${dir}/contract" >> "${LOG}" 2>&1 || { echo "could not build the mutated contract" >&2; return 2; }
    "${dir}/contract" > "${dir}/stdout" 2> "${dir}/stderr"
    local rc=$?
    echo "" >> "${LOG}"
    echo "== mutated-${name} ==" >> "${LOG}"
    echo "exit_code=${rc} expected=not 0" >> "${LOG}"
    grep -E "\[check-fail\]" "${dir}/stderr" | head -8 >> "${LOG}"
    if [ "${rc}" = "0" ]; then echo "the mutated rule '${name}' passed the contract: the contract does not hold the rule" >&2; status=1; fi
    return 0
}

run_mutation "newest" "decision.owner=(int)(count-1);decision.wait=true;   // MUTATED: the newest submission in flight"
run_mutation "always" "decision.wait=true;   // MUTATED: always wait"

{
    echo ""
    echo "baseline_exit=${baseline_rc}"
    echo "the contract holds the owner's-submission rule, and both mutated rules are caught by it"
} | tee -a "${LOG}"

echo "self-proof written to ${LOG}"
exit "${status}"
