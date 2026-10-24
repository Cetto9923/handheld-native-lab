#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN blend state contract.

Variant A: the expectation is broken (the defaults are not the ones the
            specification gives) and the final gate stays.     -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                                -> the false green
Variant C: the contract is untouched and the blend state layer stops resetting
            the state when the defaults are asked for.               -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
it is the check that keeps a SetDefaults that does nothing out, which would
leave the guest's renderer in the previous state.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
state_src = (root / 'src/graphics/nvn_device/nvn_blend_state.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The blend state layer is untouched, so
# the contract has to notice that the expectation and the implementation
# disagree.
# ---------------------------------------------------------------------------
break_anchor = 'expect_value("nvn-blend-state-default-src-rgb", (uint64_t)(uint32_t)src_rgb, 1);'
assert src.count(break_anchor) == 1, 'the blend state expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-blend-state-default-src-rgb", (uint64_t)(uint32_t)src_rgb, 0x99);')

# The final gate and the report live in nvn_blend_state_contract; touch only
# that block.
start = broke.index('static int nvn_blend_state_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_blend_state_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_blend_state_errors\":0,')
b_block = b_block.replace('           errors, (unsigned long long)stats.defaults_applied,',
                           '           (unsigned long long)stats.defaults_applied,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_blend_state_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_blend_state_broken_fixed.c').write_text(broke)
(out / 'nvn_blend_state_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the blend state layer stops
# resetting the state when the defaults are asked for. That is the lie this
# layer exists to prevent, so the contract has to go red.
# ---------------------------------------------------------------------------
c_anchor = """    if (v) {
        apply_defaults(v);
        s_stats.defaults_applied++;
    }"""
assert state_src.count(c_anchor) == 1, 'the blend state defaults anchor is not unique'
(out / 'nvn_blend_state_stub_never_defaults.c').write_text(
    state_src.replace(c_anchor, """    if (v) {
        s_stats.defaults_applied++;
    }"""))

print('wrote the three NVN blend state contract variants')
