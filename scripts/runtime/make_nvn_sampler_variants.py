#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN sampler contract.

Variant A: the expectation is broken (the contract claims a wrap mode the
            guest never set) and the final gate stays.   -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                     -> the false green
Variant C: the contract is untouched and the sampler family stops copying the
            builder's parameters into the sampler.      -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction: a
sampler is a snapshot of the builder it was made from, so a family that does
not copy the parameters would answer with values the guest never set.

The script also writes the dispatch of the family into a copy of
src/graphics/nvn_device/nvn_device.c. The dispatch of a family belongs to the
coordinator, and the contract resolves the names through the guest's own
lookup, so the self proof needs a device that hands the names out. The copy
lives in .local/selfproof/ and is used by the variants only; the file in the
tree is not touched.

Nothing is written outside .local/selfproof/.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/nvn_sampler_contract.c').read_text()
sampler_src = (root / 'src/graphics/nvn_device/nvn_sampler.c').read_text()
device_src = (root / 'src/graphics/nvn_device/nvn_device.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The sampler family is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = 'expect_value("nvn-sampler-wrap-s-round-trips", (uint64_t)wrap_s, (uint64_t)TEST_WRAP_S);'
assert src.count(break_anchor) == 1, 'the sampler expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-sampler-wrap-s-round-trips", (uint64_t)wrap_s, (uint64_t)0x0u);')

# The final gate and the report live in nvn_sampler_contract; touch only that
# block.
start = broke.index('static int nvn_sampler_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_sampler_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_sampler_errors\":0,')
# The error count is now a literal in the format string, so the argument that
# used to carry it has to go with it. The rest of the argument list keeps its
# own types: count is a size_t and the format says %zu.
b_block = b_block.replace('           errors, count, (unsigned long long)stats.initialized,',
                           '           count, (unsigned long long)stats.initialized,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_sampler_errors\":0,' in b_block, 'hardcoded error count not applied'
assert '           errors, count,' not in b_block, 'the error argument was not dropped'

(out / 'nvn_sampler_broken_fixed.c').write_text(broke)
(out / 'nvn_sampler_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the family stops copying the
# builder's parameters into the sampler.
# ---------------------------------------------------------------------------
c_anchor = '    if (!nvn_sampler_builder_copy_parameters(builder, &params)) refused = 1;'
assert sampler_src.count(c_anchor) == 1, 'the parameter copy anchor is not unique'
# The helper stays referenced, so the stub does not trip the unused warning,
# but the parameters are not copied any more.
stub = sampler_src.replace(c_anchor,
                            '    /* Variant C: the parameters of the builder are not copied any more. */\n'
                            '    (void)nvn_sampler_builder_copy_parameters;')
assert stub != sampler_src, 'the sampler stub was not written'
(out / 'nvn_sampler_stub_no_parameter_copy.c').write_text(stub)

# ---------------------------------------------------------------------------
# The dispatch of the family, in a copy of the device layer. The family block
# has the shape of the other families of that file.
# ---------------------------------------------------------------------------
include_anchor = '#include "nvn_sampler_pool.h"\n'
assert device_src.count(include_anchor) == 1, 'the sampler pool include anchor is not unique'
patched = device_src.replace(include_anchor, include_anchor + '#include "nvn_sampler.h"\n')

trap_anchor = """    /* Not implemented yet. The guest never checks the result, so it gets a
     * trap that names the API instead of a NULL. */
"""
assert patched.count(trap_anchor) == 1, 'the trap anchor is not unique'
family_block = r'''    {
        size_t count = nvn_sampler_family_count();
        for (i = 0; i < count; i++) {
            const nvn_sampler_family_entry *entry = nvn_sampler_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
'''
patched = patched.replace(trap_anchor, family_block + trap_anchor)
(out / 'nvn_device_with_sampler_family.c').write_text(patched)

print('wrote the three NVN sampler contract variants and the dispatch copy')
