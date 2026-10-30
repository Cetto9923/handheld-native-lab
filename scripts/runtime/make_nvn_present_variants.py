#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN present contract.

Variant A: an expectation is broken and the final gate stays.   -> must be RED
Variant B: the same break, the final gate is removed and the reported error
           count is hardcoded to 0.                              -> the false green
Variant C: the contract is untouched and the fake backend claims that it
           presented a frame.                                   -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
the present path may only report a frame that was really shown, so a backend
that claims success without showing anything has to be caught.

The copies of the contract live in .local/selfproof/ and are used by the
variants only; the file in the tree is not touched. The copies carry an
absolute include of the header, because a copy that sits in another directory
cannot reach the header with a relative path. The module itself is not
included by the contract any more: the build script lists it among the
sources of the host build, so the copies are linked against the module of the
tree.

Nothing is written outside .local/selfproof/.
"""
import pathlib
import re

root = pathlib.Path('.')
out = root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/nvn_present_contract.c').read_text()
header = (root / 'src/graphics/nvn_device/nvn_present.h').resolve()

# The copies live in another directory, so the relative include of the header
# has to become an absolute one.
include_anchor = '#include "../../src/graphics/nvn_device/nvn_present.h"'
assert src.count(include_anchor) == 1, 'the include of the present header is not unique'
portable = src.replace(include_anchor, '#include "%s"' % header)

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The present path is untouched, so the
# contract has to notice that the expectation and the implementation disagree.
# ---------------------------------------------------------------------------
break_pattern = re.compile(
    r'expect_value\("nvn-present-shows-nothing-when-the-step-failed",\s*'
    r'\(uint64_t\)stats\.frames_presented, \(uint64_t\)0\);')
assert len(break_pattern.findall(portable)) == 1, 'the present expectation anchor is not unique'
broke = break_pattern.sub(
    'expect_value("nvn-present-shows-nothing-when-the-step-failed", '
    '(uint64_t)stats.frames_presented, (uint64_t)1);', portable)

# The final gate and the report live in nvn_present_contract; touch only that
# block.
start = broke.index('static int nvn_present_contract(void) {')
end = broke.index('\n}\n', start) + 3
block = broke[start:end]

gate = '    CHECK(!errors);\n'
assert block.count(gate) == 1, 'the final gate anchor is not unique inside the contract'
report_anchor = r'"{\"nvn_present_errors\":%u,'
assert block.count(report_anchor) == 1, 'the report anchor is not unique inside the contract'

# Variant B: the pre-fix shape - no final gate, reported error count hardcoded.
b_block = block.replace(gate, '')
b_block = b_block.replace(report_anchor, r'"{\"nvn_present_errors\":0,')
# The error count is now a literal in the format string, so the argument that
# used to carry it has to go with it.
b_block = b_block.replace('           errors,\n', '')
assert 'CHECK(!errors);' not in b_block, 'the final gate is still in variant B'
assert r'"{\"nvn_present_errors\":0,' in b_block, 'the hardcoded error count was not applied'
assert '           errors,\n' not in b_block, 'the error argument was not dropped'

(out / 'nvn_present_broken_fixed.c').write_text(broke)
(out / 'nvn_present_broken_prefix.c').write_text(broke[:start] + b_block + broke[end:])

# ---------------------------------------------------------------------------
# C: the contract is left untouched and the fake backend lies. It claims that
# it presented a frame, which it did not do, so the contract has to catch the
# frame that was reported as shown.
# ---------------------------------------------------------------------------
stub_anchor = '    return 0; /* FAKE_SUBMIT_REFUSES */'
assert src.count(stub_anchor) == 1, 'the fake backend anchor is not unique'
stub = portable.replace(stub_anchor, '    return 1; /* FAKE_SUBMIT_REFUSES */')
assert stub != portable, 'the fake backend was not changed'
(out / 'nvn_present_stub_lies.c').write_text(stub)

print('wrote the three NVN present contract variants')
