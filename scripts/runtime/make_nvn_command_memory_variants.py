#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN command memory contract.

Variant A: the expectation is broken (the contract claims that the guest's
            callback got an event it never got) and the final gate stays.
                                                              -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                       -> the false green
Variant C: the contract is untouched and the recorder stops calling the guest.
                                                              -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
the memory callback of the guest is the guest's own function, and a recorder
that does not call it never asks the guest for the command memory the guest
offers, so the recording can only continue in a segment of the host's memory.

Nothing outside .local/selfproof/ is written.
"""
import pathlib

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/nvn_command_memory_contract.c').read_text()
buffer_src = (root / 'src/graphics/nvn_device/nvn_command_buffer.c').read_text()

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The recorder is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_anchor = ('expect_value("command-memory-the-callback-got-the-out-of-memory-event", '
                 's_callback_words[1], 0);')
assert src.count(break_anchor) == 1, 'the command memory expectation anchor is not unique'
broke = src.replace(break_anchor,
                    'expect_value("command-memory-the-callback-got-the-out-of-memory-event", '
                    's_callback_words[1], 1);')

# The final gate and the report live at the end of the case; touch only that
# block.
gate = ('    if(errors) {\n'
        '        fprintf(stderr,"[check-fail] the command memory contract found %u failed '
        'expectations\\n",errors);\n'
        '        return 1;\n'
        '    }\n')
assert broke.count(gate) == 1, 'final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_command_memory_errors\":%u,'
assert broke.count(report_anchor) == 1, 'report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_broke = broke.replace(gate, '')
b_broke = b_broke.replace(report_anchor, r'"{\"nvn_command_memory_errors\":0,')
# The error count is now a literal in the format string, so the argument that
# used to carry it has to go with it. The rest of the argument list keeps its
# own types.
b_broke = b_broke.replace('           errors,(unsigned long long)stats.packages,',
                           '           (unsigned long long)stats.packages,')
assert 'if(errors) {' not in b_broke, 'final gate still present in variant B'
assert r'"{\"nvn_command_memory_errors\":0,' in b_broke, 'hardcoded error count not applied'
assert '           errors,(unsigned long long)' not in b_broke, 'the error argument was not dropped'

(out / 'nvn_command_memory_broken_fixed.c').write_text(broke)
(out / 'nvn_command_memory_broken_prefix.c').write_text(b_broke)

# ---------------------------------------------------------------------------
# Variant C: the contract is left untouched and the recorder stops calling the
# guest.
# ---------------------------------------------------------------------------
c_anchor = '        rc = call((void *)(uintptr_t)callback, x, out_x);'
assert buffer_src.count(c_anchor) == 1, 'the guest call anchor is not unique'
(out / 'nvn_command_buffer_stub_no_guest_call.c').write_text(
    buffer_src.replace(c_anchor,
                       '        rc = NVN_CB_GUEST_CALL_REFUSED; /* the self proof: the recorder stops '
                       'calling the guest */\n        (void)call;'))

print('wrote the three NVN command memory contract variants')
