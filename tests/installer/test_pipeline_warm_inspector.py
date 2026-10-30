import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'warm_inspector', ROOT / 'scripts/distribution/inspect_pipeline_warm_list.py')
warm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(warm)
KEY = '0123456789abcdef'
# Independent fixed value from the existing C++ warm::fnv algorithm.
IDENTITY = 0xbfad94ed2e171b5f


def record(identity=IDENTITY, version=1):
    words = [version, 0, 0, 0, 0, 1, 0, 16,
             identity & 0xffffffff, identity >> 32]
    return (struct.pack('<III', 0x57504c58, 1, 16) + KEY.encode()
            + struct.pack('<I', len(words))
            + struct.pack('<' + 'I' * len(words), *words))


class WarmInspection(unittest.TestCase):
    def test_framing_and_fixed_shader_identity(self):
        self.assertEqual(warm.module_id(KEY), IDENTITY)
        result = warm.inspect(record(), {KEY})
        self.assertEqual(result['records'], 1)
        self.assertEqual(result['unique_records'], 1)
        self.assertEqual(result['missing_shader_keys'], [])
        self.assertIn('not GPU', result['validation'])

    def test_duplicates_are_reported_without_becoming_unique(self):
        result = warm.inspect(record() * 2)
        self.assertEqual(result['records'], 2)
        self.assertEqual(result['unique_records'], 1)

    def test_missing_archive_key_is_explicit(self):
        self.assertEqual(warm.inspect(record(), set())['missing_shader_keys'], [KEY])

    def test_every_truncated_prefix_is_refused(self):
        raw = record()
        for length in range(len(raw)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                warm.inspect(raw[:length])

    def test_identity_and_key_version_mismatch_are_refused(self):
        for raw in [record(identity=IDENTITY ^ 1), record(version=2)]:
            with self.assertRaises(ValueError):
                warm.inspect(raw)

    def test_lengths_and_stage_counts_are_bounded_before_read(self):
        for offset, value in [(0, 0), (4, 6), (8, 0xffffffff), (28, 0xffffffff)]:
            raw = bytearray(record())
            struct.pack_into('<I', raw, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                warm.inspect(bytes(raw))

    def test_piece_bounds_and_stage_count_mismatch_are_refused(self):
        for word, value in [(1, 999), (5, 2)]:
            raw = bytearray(record())
            struct.pack_into('<I', raw, 32 + word * 4, value)
            with self.assertRaises(ValueError):
                warm.inspect(bytes(raw))

    def test_archive_table_bounds_and_order(self):
        table = struct.pack('<16sQQ', KEY.encode(), 48, 32)
        good = struct.pack('<8sII', b'HNLXGS1\0', 1, 0) + table + b'x' * 32
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'archive'
            path.write_bytes(good)
            self.assertEqual(warm.archive_keys(path), {KEY})
            for raw in [good[:-1], good + b'x', good[:16],
                        good[:16] + struct.pack('<16sQQ', KEY.encode(), 49, 32) + b'x' * 32]:
                path.write_bytes(raw)
                with self.assertRaises(ValueError):
                    warm.archive_keys(path)


if __name__ == '__main__':
    unittest.main()
