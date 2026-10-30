#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN memory pool contract.

Variant A: the expectation is broken (this build has to be able to create a
            pool) and the final gate stays.                    -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                           -> the false green
Variant C: the contract is untouched and the memory pool layer claims a pool
            even when there is no real device behind it.       -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
it is the check that keeps the memory pool layer from handing out a pool that
the graphics device cannot use, which is the failure mode the guest would hit
as memory that never reaches the screen.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
pool_src = (root / 'src/graphics/nvn_device/nvn_memory_pool.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The memory pool layer is untouched, so
# the contract has to notice that the expectation and the real device disagree.
# ---------------------------------------------------------------------------
break_anchor = '#define NVN_MEMORY_POOL_EXPECT_REAL_POOL 0'
assert src.count(break_anchor) == 1, 'the memory pool expectation anchor is not unique'
broke = src.replace(break_anchor, '#define NVN_MEMORY_POOL_EXPECT_REAL_POOL 1')

# The final gate and the report live in nvn_memory_pool_contract; touch only
# that block.
start = broke.index('static int nvn_memory_pool_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_memory_pool_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_memory_pool_errors\":0,')
b_block = b_block.replace('           errors, pool_result ? "true" : "false",',
                           '           pool_result ? "true" : "false",')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_memory_pool_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_memory_pool_broken_fixed.c').write_text(broke)
(out / 'nvn_memory_pool_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the memory pool layer stops
# refusing a pool without a device. That is the lie the whole layer exists to
# prevent, so the contract has to go red.
# ---------------------------------------------------------------------------
c_anchor = '    if (!nvn_device_is_ready(b->device)) {'
assert pool_src.count(c_anchor) == 1, 'the memory pool refusal anchor is not unique'
(out / 'nvn_memory_pool_stub_always_succeeds.c').write_text(
    pool_src.replace(c_anchor, '    if (0 && !nvn_device_is_ready(b->device)) {'))

print('wrote the three NVN memory pool contract variants')
