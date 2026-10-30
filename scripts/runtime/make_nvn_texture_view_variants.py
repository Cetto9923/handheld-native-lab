#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN texture view contract.

Variant A: the expectation is broken (the contract claims a base level the
            guest never set) and the final gate stays.        -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                            -> the false green
Variant C: the contract is untouched and the view family stops recording the
            levels of a view.                                   -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction: the
getters of a view say whether the guest set a field at all, so a family that
does not record the field would answer "not set" for a view the guest did
describe.

The script also writes the dispatch of the three families into a copy of
src/graphics/nvn_device/nvn_device.c: the texture family, the texture view
family and the three device handle functions. The dispatch of a family belongs
to the coordinator, and the contract resolves the names through the guest's own
lookup, so the self proof needs a device that hands the names out. The copy
lives in .local/selfproof/ and is used by the variants only; the file in the
tree is not touched.

Nothing is written outside .local/selfproof/.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/nvn_texture_view_contract.c').read_text()
view_src = (root / 'src/graphics/nvn_device/nvn_texture_view.c').read_text()
device_src = (root / 'src/graphics/nvn_device/nvn_device.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The view family is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = ('expect_value("nvn-texture-view-base-level-round-trips", (uint64_t)(uint32_t)base_level,\n'
                '                 (uint64_t)TEST_BASE_LEVEL);')
assert src.count(break_anchor) == 1, 'the view expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-texture-view-base-level-round-trips", (uint64_t)(uint32_t)base_level,\n'
                    '                 (uint64_t)64);')

# The final gate and the report live in nvn_texture_view_contract; touch only
# that block.
start = broke.index('static int nvn_texture_view_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_texture_view_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_texture_view_errors\":0,')
# The error count is now a literal in the format string, so the argument that
# used to carry it has to go with it. The rest of the argument list keeps its
# own types: count is a size_t and the format says %zu.
b_block = b_block.replace('           errors, count, (unsigned long long)stats.set_defaults,',
                           '           count, (unsigned long long)stats.set_defaults,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_texture_view_errors\":0,' in b_block, 'hardcoded error count not applied'
assert '           errors, count,' not in b_block, 'the error argument was not dropped'

(out / 'nvn_texture_view_broken_fixed.c').write_text(broke)
(out / 'nvn_texture_view_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the family stops recording the
# levels of a view.
# ---------------------------------------------------------------------------
c_anchor = """    if (v) {
        v->set_levels = 1;
        v->base_level = base;
        v->nlevels = n;
    } else {"""
assert view_src.count(c_anchor) == 1, 'the levels anchor is not unique'
stub = view_src.replace(c_anchor,
                        """    if (v) {
        /* Variant C: the levels of the view are not recorded. */
        (void)base;
        (void)n;
    } else {""")
assert stub != view_src, 'the view stub was not written'
(out / 'nvn_texture_view_stub_no_levels.c').write_text(stub)

# ---------------------------------------------------------------------------
# The dispatch of the three families, in a copy of the device layer. The family
# blocks have the shape of the other families of that file.
# ---------------------------------------------------------------------------
include_anchor = '#include "nvn_texture_builder.h"\n'
assert device_src.count(include_anchor) == 1, 'the texture builder include anchor is not unique'
patched = device_src.replace(include_anchor, include_anchor + '#include "nvn_texture.h"\n#include "nvn_texture_view.h"\n')

trap_anchor = """    /* Not implemented yet. The guest never checks the result, so it gets a
     * trap that names the API instead of a NULL. */
"""
assert patched.count(trap_anchor) == 1, 'the trap anchor is not unique'
family_block = r'''    {
        size_t count = nvn_texture_family_count();
        for (i = 0; i < count; i++) {
            const nvn_texture_family_entry *entry = nvn_texture_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_texture_view_family_count();
        for (i = 0; i < count; i++) {
            const nvn_texture_view_family_entry *entry = nvn_texture_view_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_device_handle_family_count();
        for (i = 0; i < count; i++) {
            const nvn_device_handle_family_entry *entry = nvn_device_handle_family_entry_at(i);
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
(out / 'nvn_device_with_texture_view_family.c').write_text(patched)

print('wrote the three NVN texture view contract variants and the dispatch copy')
