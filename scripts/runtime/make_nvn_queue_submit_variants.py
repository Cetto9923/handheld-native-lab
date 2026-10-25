#!/usr/bin/env python3
"""Build the three self-proof variants of the NVN queue submit contract.

Variant A: the expectation is broken (the replay has to have read one more
            package than before the submission) and the final gate stays.
                                                        -> must be RED
Variant B: the same break, the final gate is removed and the reported error
            count is hardcoded to 0.                   -> the false green
Variant C: the contract is untouched and the stub is broken: the queue layer
            does not call the replay any more, so the seam is not joined.
                                                        -> must be RED

A and B differ only in the final gate, so a single variant run cannot tell
"fixed" from "the assertion was always true". C closes the other direction:
it is the check that keeps the queue layer from dropping the seam - the
submission that reads no package at all - which is the failure mode that
would look like a working port on the screen and be nothing on the GPU.

The output directory is the first argument, so that every run keeps its
variants in a directory of its own and no log is overwritten.
"""
import pathlib
import sys

root = pathlib.Path('.')
out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / '.local/selfproof'
out.mkdir(parents=True, exist_ok=True)

src = (root / 'tests/native/nvn_queue_submit_contract.c').read_text()
queue_src = (root / 'src/graphics/nvn_device/nvn_queue.c').read_text()

# The contract names its headers with a path that is relative to the
# directory of the contract itself. A variant is written into the directory
# of the run, which is elsewhere, so the relative path would not resolve.
# The path is rewritten to the one that the include path of the build
# carries, and the build adds the root of the checkout to its include path.
def rewrite_includes(text):
    assert text.count('#include "../../src/') > 0, 'the contract has no relative include'
    return text.replace('#include "../../src/', '#include "src/')

# ---------------------------------------------------------------------------
# A and B: break the expectation only. The queue layer is untouched, so the
# contract has to notice that the expectation and the real behaviour disagree.
# ---------------------------------------------------------------------------
break_anchor = """        expect_value("queue-submit-the-replay-read-one-more-package",
                     after.packages-before.packages,1);"""
assert src.count(break_anchor) == 1, 'the package count expectation is not unique'
broke = src.replace(break_anchor, """        expect_value("queue-submit-the-replay-read-one-more-package",
                     after.packages-before.packages,2);""")

# The final gate and the report live at the end of the contract; touch only
# those.
gate = '    CHECK(!errors);\n'
assert broke.count(gate) == 1, 'final gate anchor is not unique'
report_anchor = '"{\\"nvn_queue_submit_errors\\":%u,'
assert broke.count(report_anchor) == 1, 'report anchor is not unique'
# The argument that goes with the count. It goes with it, so it has to go
# with it: leaving it would shift every argument of the printf and the
# build would fail on the format, not on the contract.
errors_arg = '               errors,\n'
assert broke.count(errors_arg) == 1, 'the error count argument is not unique'

# Variant B: the pre-fix shape - no final gate, reported error count
# hardcoded. The stdout then still says errors:0 while the stderr keeps the
# real [check-fail] line: that is the false green, and it has to be measured.
b_broke = broke.replace(gate, '')
b_broke = b_broke.replace(report_anchor, '"{\\"nvn_queue_submit_errors\\":0,')
b_broke = b_broke.replace(errors_arg, '')
assert 'CHECK(!errors);' not in b_broke, 'final gate still present in variant B'
assert '"{\\"nvn_queue_submit_errors\\":0,' in b_broke, 'hardcoded error count not applied'

(out / 'nvn_queue_submit_broken_fixed.c').write_text(rewrite_includes(broke))
(out / 'nvn_queue_submit_broken_prefix.c').write_text(rewrite_includes(b_broke))

# ---------------------------------------------------------------------------
# C: the contract is left untouched and the stub stops calling the replay.
# The seam is what this whole card is about: a submission that reads no
# package must not look like a working one.
# ---------------------------------------------------------------------------
c_anchor = '        (void)nvn_command_replay_run(handle);'
assert queue_src.count(c_anchor) == 1, 'the replay call anchor is not unique'
(out / 'nvn_queue_submit_stub_without_the_replay.c').write_text(
    queue_src.replace(c_anchor, '        /* the seam is broken on purpose: the replay is not called */'))

print('wrote the three NVN queue submit contract variants into ' + str(out))
sys.exit(0)
