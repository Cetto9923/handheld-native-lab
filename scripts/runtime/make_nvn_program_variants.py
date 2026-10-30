#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN program contract.

Variant A: the expectation is broken (the contract claims that the control
            section was not copied) and the final gate stays. -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                       -> the false green
Variant C: the contract is untouched and the program layer stops copying the
            control section the guest handed over.        -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
the control section is only valid while the guest is inside the call that
hands it over, so a layer that does not copy it would be reading a dangling
pointer as soon as the guest returns.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
program_src = (root / 'src/graphics/nvn_device/nvn_program.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The program layer is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = ('expect_value("nvn-program-control-section-was-copied", '
                '(uint64_t)(uint32_t)copied, 1);')
assert src.count(break_anchor) == 1, 'the program expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-program-control-section-was-copied", '
                    '(uint64_t)(uint32_t)copied, 0);')

# The final gate and the report live in nvn_program_contract; touch only that
# block.
start = broke.index('static int nvn_program_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_program_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_program_errors\":0,')
b_block = b_block.replace('           errors, (unsigned long long)stats.initialized,',
                           '           (unsigned long long)stats.initialized,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_program_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_program_broken_fixed.c').write_text(broke)
(out / 'nvn_program_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the program layer stops
# copying the control section. The control section is only guaranteed to be
# valid while the guest is inside the call that hands it over, so a layer that
# does not copy it would be reading memory the guest may reuse the moment it
# returns.
# ---------------------------------------------------------------------------
c_anchor = '    memcpy(p->stages[stage].control, control, control_length);'
assert program_src.count(c_anchor) == 1, 'the program control copy anchor is not unique'
(out / 'nvn_program_stub_no_control_copy.c').write_text(
    program_src.replace(c_anchor,
                        '    memset(p->stages[stage].control, 0, sizeof(p->stages[stage].control));'))

print('wrote the three NVN program contract variants')
