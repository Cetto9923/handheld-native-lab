#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN builder contract.

Variant A: the expectation is broken (the contract claims a different default
            wrap mode than the reference host uses) and the final gate stays.
                                                                  -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                            -> the false green
Variant C: the contract is untouched and the builder layer stops applying the
            defaults.                                           -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
the defaults of a builder have to be the ones the reference host that runs
this game uses, so a layer that does not apply them would hand the guest a
sampler or a texture that does not match what the game expects.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
sampler_src = (root / 'src/graphics/nvn_device/nvn_sampler_builder.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The builder layer is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = 'expect_value("nvn-builder-sampler-default-wrap-s", (uint64_t)(uint32_t)wrap_s, 0x1);'
assert src.count(break_anchor) == 1, 'the builder expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-builder-sampler-default-wrap-s", (uint64_t)(uint32_t)wrap_s, 0x7);')

# The final gate and the report live in nvn_builder_contract; touch only that
# block.
start = broke.index('static int nvn_builder_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_builder_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_builder_errors\":0,')
b_block = b_block.replace('           errors, sampler_count, texture_count,',
                           '           (unsigned long long)sampler_count, (unsigned long long)texture_count,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_builder_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_builder_broken_fixed.c').write_text(broke)
(out / 'nvn_builder_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the builder layer stops
# applying the defaults.
# ---------------------------------------------------------------------------
c_anchor = '        apply_defaults(s);'
assert sampler_src.count(c_anchor) == 1, 'the sampler defaults anchor is not unique'
(out / 'nvn_sampler_builder_stub_no_defaults.c').write_text(
    sampler_src.replace(c_anchor, '        (void)s;'))

print('wrote the three NVN builder contract variants')
