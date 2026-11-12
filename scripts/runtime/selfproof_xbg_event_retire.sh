#!/usr/bin/env bash
# The self-proof of the retirement rule for a deferred event.
#
#   baseline = the contract with the production header                   -> must be GREEN
#   mutated  = the contract with a header that answers the OLDEST signal -> must be RED
#
# The mutation is the rule the engine must not follow: the oldest signal to an address is not the value the guest is
# entitled to read, so a rule that picked it would publish a value the guest never signalled last, and the guest's own
# clock would run on the wrong signal. A contract that also passes on the mutated header would prove nothing.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}" || exit 99

BUILD="${FFRS_SELF_PROOF_BUILD:-.local/build-xbg-event-retire-selfproof}"
LOG="${FFRS_SELF_PROOF_LOG:-.local/logs/xbg-event-retire-selfproof.log}"
RUN_ID="$(date '+%Y%m%d-%H%M%S')-$$"
RUN_OUT="${FFRS_SELF_PROOF_DIR:-.local/selfproof}/run-${RUN_ID}"
mkdir -p "${BUILD}" "${RUN_OUT}" "$(dirname "${LOG}")" || exit 98
LOG="${RUN_OUT}/selfproof.log"
: > "${LOG}"

STRICT="-std=c++17 -O2 -g -Wall -Wextra -Werror"
HEADER="src/graphics/nvn_vulkan/engine/xbg_event_retire.h"
CONTRACT="tests/native/xbg_event_retire_contract.cpp"

{
    echo "The self-proof of the event retirement rule"
    echo "  baseline = the contract with the production header                   -> must be GREEN"
    echo "  mutated  = the contract with a header that answers the OLDEST signal -> must be RED"
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

# The mutation: the rule that answers the oldest signal instead of the newest one.
mkdir -p "${RUN_OUT}/mutated"
sed 's/signals\[i\]\.order>best_order/signals[i].order<best_order/' "${HEADER}" > "${RUN_OUT}/mutated/xbg_event_retire.h" || exit 93
if cmp -s "${HEADER}" "${RUN_OUT}/mutated/xbg_event_retire.h"; then echo "the mutation did not change the header" >&2; exit 93; fi
sed "s|"\.\./\.\./src/graphics/nvn_vulkan/engine/xbg_event_retire.h"|"xbg_event_retire.h"|" "${CONTRACT}" > "${RUN_OUT}/mutated/contract.cpp" || exit 93
clang++ ${STRICT} "${RUN_OUT}/mutated/contract.cpp" -o "${RUN_OUT}/mutated/contract" >> "${LOG}" 2>&1 || { echo "could not build the mutated contract" >&2; exit 92; }
"${RUN_OUT}/mutated/contract" > "${RUN_OUT}/mutated.stdout" 2> "${RUN_OUT}/mutated.stderr"
mutated_rc=$?
echo "" >> "${LOG}"
echo "== mutated ==" >> "${LOG}"
echo "exit_code=${mutated_rc} expected=not 0" >> "${LOG}"
grep -E "\[check-fail\]" "${RUN_OUT}/mutated.stderr" | head -8 >> "${LOG}"
if [ "${mutated_rc}" = "0" ]; then echo "the mutated rule passed the contract: the contract does not hold the rule" >&2; status=1; fi

{
    echo ""
    echo "baseline_exit=${baseline_rc} mutated_exit=${mutated_rc}"
    echo "the contract holds the newest-signal rule, and the mutated rule is caught by it"
} | tee -a "${LOG}"

echo "self-proof written to ${LOG}"
exit "${status}"
