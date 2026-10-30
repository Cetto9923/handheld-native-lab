#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN device information contract.

Variant A: the expectation is broken (the device has to report the texture pool
            limit the game sizes its own pool by) and the final gate stays.
                                                              -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                          -> the false green
Variant C: the contract is untouched and the device layer answers zero for
            every limit, the way it did before the table was added.
                                                              -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction: it
is the check that keeps the device layer from answering zero for the limits,
which is what aborted the game in its own NVNHelpers.cpp:240 (there are no
available TextureIDs in the pool) in the device run of p-mm-34.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
device_src = (root / 'src/graphics/nvn_device/nvn_device.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The device layer is untouched, so the
# contract has to notice that the expectation and the real device disagree.
# ---------------------------------------------------------------------------
break_anchor = ('expect_value("nvn-device-get-integer-max-texture-pool-size", '
                '(uint64_t)(uint32_t)limit, 1048576);')
assert src.count(break_anchor) == 1, 'the device information expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("nvn-device-get-integer-max-texture-pool-size", '
                    '(uint64_t)(uint32_t)limit, 0);')

# The final gate and the report live in nvn_device_contract; touch only that
# block.
start = broke.index('static int nvn_device_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_device_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_device_errors\":0,')
b_block = b_block.replace('           errors, stats.real_device ? "true" : "false",',
                           '           stats.real_device ? "true" : "false",')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_device_errors\":0,' in b_block, 'hardcoded error count not applied'

(out / 'nvn_device_info_broken_fixed.c').write_text(broke)
(out / 'nvn_device_info_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the device layer answers zero
# for every limit in the table, the way it did before the table was there. That
# is the lie that aborted the game, so the contract has to go red.
#
# The table is still read, so the stub compiles under the same strict flags as
# the file it comes from: an unread table is an error there, and a self-proof
# that needs a laxer compiler would not prove much.
# ---------------------------------------------------------------------------
c_anchor = '        *out_value = g_device_info[w];'
assert device_src.count(c_anchor) == 1, 'the device information table anchor is not unique'
c_stub = ('        /* The self-proof stub: every limit is answered with zero, the way the\n'
          '         * device layer answered it before the table was added. */\n'
          '        *out_value = g_device_info[w] - g_device_info[w];')
(out / 'nvn_device_info_stub_zero_limits.c').write_text(device_src.replace(c_anchor, c_stub))

print('wrote the three NVN device information contract variants')
