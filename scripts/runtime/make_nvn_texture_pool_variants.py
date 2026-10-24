#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN texture pool contract.

Variant A: the expectation is broken (this build has to be able to create a
            texture pool) and the final gate stays.               -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                                -> the false green
Variant C: the contract is untouched and the texture pool layer hands out a
            pool even when there is no memory pool behind it.       -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
it is the check that keeps the texture pool layer from handing out a pool that
has no memory behind it, which is the failure mode the guest would hit as
textures that address nothing.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
pool_src = (root / 'src/graphics/nvn_device/nvn_texture_pool.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The texture pool layer is untouched, so
# the contract has to notice that the expectation and the real device disagree.
# ---------------------------------------------------------------------------
break_anchor = 'expect_value("nvn-texture-pool-init-succeeds", pool_result, 1);'
assert src.count(break_anchor) == 1, 'the texture pool expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-texture-pool-init-succeeds", pool_result, 0);')

# The final gate and the report live in nvn_texture_pool_contract; touch only
# that block.
start = broke.index('static int nvn_texture_pool_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_texture_pool_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_texture_pool_errors\":0,')
b_block = b_block.replace('           errors, (unsigned long long)stats.initialized,',
                           '           (unsigned long long)stats.initialized,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_texture_pool_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_texture_pool_broken_fixed.c').write_text(broke)
(out / 'nvn_texture_pool_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the texture pool layer stops
# refusing a pool without a memory pool. That is the lie the whole layer exists
# to prevent, so the contract has to go red.
# ---------------------------------------------------------------------------
c_anchor = '    if (!memory_pool) {'
assert pool_src.count(c_anchor) == 1, 'the texture pool refusal anchor is not unique'
(out / 'nvn_texture_pool_stub_always_succeeds.c').write_text(
    pool_src.replace(c_anchor, '    if (0 && !memory_pool) {'))

print('wrote the three NVN texture pool contract variants')
