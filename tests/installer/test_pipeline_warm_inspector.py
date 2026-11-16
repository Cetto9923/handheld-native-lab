import importlib.util
import io
import itertools
from pathlib import Path
import re
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


def record(identity=IDENTITY, version=1, key=KEY):
    words = [version, 0, 0, 0, 0, 1, 0, 16,
             identity & 0xffffffff, identity >> 32]
    return (struct.pack('<III', 0x57504c58, 1, len(key)) + key.encode()
            + struct.pack('<I', len(words))
            + struct.pack('<' + 'I' * len(words), *words))


class WarmInspection(unittest.TestCase):
    def test_all_canonical_family_combinations_keep_the_source_archive_key(self):
        families = ['ib1:0_74,4294967295_98', 'stb1:1_13,2_109',
                    'vin1:0_0,7_1,15_2', 'lf1:52_100,106_100']
        for size in range(5):
            for combination in itertools.combinations(families, size):
                key = KEY + ''.join(':' + family for family in combination)
                with self.subTest(key=key):
                    self.assertEqual(warm.shader_source_key(key), KEY)
                    raw = record(identity=warm.module_id(key), key=key)
                    result = warm.inspect(raw, {KEY})
                    self.assertEqual(result['missing_shader_keys'], [])
                    self.assertEqual(result['shader_keys'], 1)

    def test_typed_vk_formats_match_the_production_spirv_mapping(self):
        header = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h').read_text()
        constants = dict((name, int(value)) for name, value in re.findall(
            r'(XBG_(?:VK_FORMAT|IMAGE_FORMAT)_[A-Z0-9_]+)=([0-9]+)', header))
        function = header.split('inline uint32_t xbg_spirv_image_format(', 1)[1].split('\n}', 1)[0]
        expected = {constants[vk]: constants[spirv] for vk, spirv in re.findall(
            r'case (XBG_VK_FORMAT_[A-Z0-9_]+):return (XBG_IMAGE_FORMAT_[A-Z0-9_]+);', function)}
        self.assertEqual(len(expected), 24)
        self.assertEqual(warm.SPIRV_IMAGE_FORMATS, expected)
        self.assertEqual(warm.SPIRV_IMAGE_FORMATS[98], 33)
        for format_value in expected:
            with self.subTest(format_value=format_value):
                self.assertEqual(warm.shader_source_key(KEY + ':stb1:0_' + str(format_value)), KEY)

    def test_identity_length_has_the_same_inclusive_256_byte_bound(self):
        tail = ','.join(str(binding) + '_98' for binding in range(1000000000, 1000000016))
        boundary = KEY + ':ib1:99999999_98,' + tail
        oversized = KEY + ':ib1:999999999_98,' + tail
        self.assertEqual(len(boundary), 256)
        self.assertEqual(len(oversized), 257)
        self.assertEqual(warm.shader_source_key(boundary), KEY)
        self.assertEqual(warm.inspect(record(identity=warm.module_id(boundary), key=boundary))['records'], 1)
        for key in [oversized, KEY[:-1], KEY.upper(), KEY + '\n']:
            with self.subTest(key=key), self.assertRaises(ValueError):
                warm.shader_source_key(key)
        with self.assertRaises(ValueError):
            warm.inspect(record(identity=warm.module_id(oversized), key=oversized))

    def test_noncanonical_family_order_and_duplicates_are_refused(self):
        families = ['ib1:0_74', 'stb1:0_98', 'vin1:0_2', 'lf1:52_100']
        bad = [':ib2:0_74', ':unknown:0_74', ':IB1:0_74', ':ib1:',
               ':ib1:0_74:', '::ib1:0_74', ':vin1:0_2:stb1:0_98']
        for family in families:
            bad.append(':' + family + ':' + family)
        for order in itertools.permutations(families):
            if list(order) != families:
                bad.append(':' + ':'.join(order))
        for suffix in bad:
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                warm.shader_source_key(KEY + suffix)

    def test_recipe_numeric_bounds_order_and_exact_vk_formats(self):
        bad = [
            ':ib1:00_74', ':ib1:1_074', ':ib1:+1_74', ':ib1:-1_74',
            ':ib1:4294967296_74', ':ib1:1_4294967296', ':ib1:1_75',
            ':ib1:0_74,0_98', ':ib1:1_74,0_98', ':ib1:0_74,',
            ':ib1:0_74,,1_98', ':ib1:0__74', ':ib1:0_74_98',
            ':ib1:0_74 1_98', ':ib1:１_74', ':ib1:0_74\n',
            ':stb1:0_0', ':stb1:0_3', ':stb1:0_33', ':stb1:0_110',
            ':stb1:1_98,1_99', ':stb1:2_98,1_99', ':stb1:0_098',
            ':vin1:16_2', ':vin1:4294967295_2', ':vin1:0_3',
            ':vin1:0_4294967296', ':vin1:00_2', ':vin1:0_02',
            ':vin1:0_2,0_1', ':vin1:15_2,14_1', ':vin1:0_2,',
            ':lf1:0_103', ':lf1:0_0', ':lf1:0_100,0_100',
            ':lf1:01_100', ':lf1:0_0100', ':lf1:52_100:vin1:0_2',
        ]
        for suffix in bad:
            with self.subTest(suffix=suffix), self.assertRaises(ValueError):
                warm.shader_source_key(KEY + suffix)
        for suffix in [':ib1:4294967295_98', ':stb1:4294967295_109',
                       ':vin1:0_0,1_1,15_2']:
            self.assertEqual(warm.shader_source_key(KEY + suffix), KEY)

    def test_composite_identity_hash_uses_the_complete_identity(self):
        key = KEY + ':ib1:0_74:stb1:1_98:vin1:0_2'
        with self.assertRaises(ValueError):
            warm.inspect(record(identity=IDENTITY, key=key), {KEY})
        self.assertEqual(warm.inspect(record(identity=warm.module_id(key), key=key),
                                     set())['missing_shader_keys'], [KEY])

    def test_archive_inspection_never_reads_payload(self):
        index = (struct.pack('<8sII', b'HNLXGS1\0', 1, 0)
                 + struct.pack('<16sQQ', KEY.encode(), 48, 32))

        class IndexOnly(io.BytesIO):
            def read(self, size=-1):
                if size < 0 or self.tell() + size > len(index):
                    raise AssertionError('archive payload read')
                return super().read(size)

        self.assertEqual(warm.archive_keys_from_file(IndexOnly(index), 80), {KEY})

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
