#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN command buffer contract.

Variant A: the expectation is broken (the contract claims that the record
            does not hold the colour the guest handed over) and the final gate
            stays.                                             -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                          -> the false green
Variant C: the contract is untouched and the recorder stops copying the colour
            out of the guest's memory.                        -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
the colour lives in the guest's own memory, and the guest may write over it
the moment the call returns, so a record that only named the address would be
unreadable by the time the command is run.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/guest_process_contract.c').read_text()
buffer_src = (root / 'src/graphics/nvn_device/nvn_command_buffer.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The recorder is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = 'expect_condition("nvn-command-buffer-record-holds-the-colour", equal);'
assert src.count(break_anchor) == 1, 'the command buffer expectation anchor is not unique'
broke = src.replace(break_anchor, 'expect_condition("nvn-command-buffer-record-holds-the-colour", !equal);')

# The final gate and the report live in nvn_command_buffer_contract; touch only
# that block.
start = broke.index('static int nvn_command_buffer_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_command_buffer_errors\":%u,'
assert block.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_command_buffer_errors\":0,')
# The error count is now a literal in the format string, so the argument that
# used to carry it has to go with it. The rest of the argument list keeps its
# own types: count is a size_t and the format says %zu.
b_block = b_block.replace('           errors, count, (unsigned long long)stats.calls,',
                           '           count, (unsigned long long)stats.calls,')
assert 'CHECK(!errors);' not in b_block, 'final gate still present in variant B'
assert r'"{\"nvn_command_buffer_errors\":0,' in b_block, 'hardcoded error count not applied'
assert '           errors, count,' not in b_block, 'the error argument was not dropped'

(out / 'nvn_command_buffer_broken_fixed.c').write_text(broke)
(out / 'nvn_command_buffer_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the recorder stops copying the
# colour out of the guest's memory.
# ---------------------------------------------------------------------------
c_anchor = '        memcpy(record->rgba, rgba, sizeof(record->rgba));'
assert buffer_src.count(c_anchor) == 1, 'the colour copy anchor is not unique'
(out / 'nvn_command_buffer_stub_no_colour_copy.c').write_text(
    buffer_src.replace(c_anchor, '        memset(record->rgba, 0, sizeof(record->rgba));'))

print('wrote the three NVN command buffer contract variants')
