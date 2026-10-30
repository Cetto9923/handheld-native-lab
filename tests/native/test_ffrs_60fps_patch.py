"""Self-owned NSO fixtures only; no original game byte ranges are included."""
import contextlib
from dataclasses import replace
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/graphics/prepare_ffrs_60fps.py'
SPEC = importlib.util.spec_from_file_location('ffrs_60fps_fixture_module', SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class GuardedCopyContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ffrs-self-owned-nso-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.local = self.root / '.local'
        self.local.mkdir()
        self.addCleanup(patch.stopall)
        patch.object(MODULE, 'LOCAL_ROOT', self.local).start()
        # Synthetic pattern, segment layout, BuildId and words. Nothing is
        # copied from a game module or its instruction context.
        self.data = bytearray((i * 37 + 11) % 256 for i in range(0x400))
        self.data[:0x100] = bytes(0x100)
        self.data[:4] = b'NSO0'
        struct.pack_into('<III', self.data, 0x10, 0x100, 0x4000, 0x200)
        struct.pack_into('<III', self.data, 0x20, 0x300, 0x6000, 0x80)
        struct.pack_into('<III', self.data, 0x30, 0x380, 0x7000, 0x80)
        build = bytes(range(32))
        self.data[0x40:0x60] = build
        self.data[0x180:0x184] = b'\x11\x22\x33\x44'
        self.profile = MODULE.PatchProfile(
            build.hex(), 0x4080, b'\x11\x22\x33\x44', b'\x55\x66\x77\x88',
            0x4040, 160, hashlib.sha256(self.data[0x140:0x1e0]).hexdigest())
        self.source = self.root / 'self-owned.nso'
        self.source.write_bytes(self.data)
        self.out = self.local / 'derived'

    def prepare(self, profile=None):
        return MODULE.prepare(self.source, self.out, profile=profile or self.profile)

    def refused(self, *, profile=None, message=None):
        before = self.source.read_bytes()
        with self.assertRaisesRegex(MODULE.PreparationError, message or '.'):
            self.prepare(profile)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertFalse(self.out.exists(), 'validation failure must precede any output creation')

    def test_exact_one_word_source_and_build_id_preserved(self):
        before = self.source.read_bytes()
        manifest = self.prepare()
        after = (self.out / 'main.nso').read_bytes()
        expected = bytearray(before)
        expected[0x180:0x184] = self.profile.replacement_word
        self.assertEqual(after, expected)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertEqual(after[0x40:0x60], before[0x40:0x60])
        self.assertEqual([i for i, (a, b) in enumerate(zip(before, after)) if a != b],
                         list(range(0x180, 0x184)))
        self.assertEqual(manifest['source']['sha256'], hashlib.sha256(before).hexdigest())
        self.assertEqual(manifest['derived']['sha256'], hashlib.sha256(after).hexdigest())
        self.assertEqual(manifest['patch']['file_offset'], 0x180)
        self.assertEqual(manifest['status'], 'VERIFIED_DERIVED_COPY_NOT_RUN')
        self.assertTrue(manifest['derived']['is_modified_text'])
        self.assertTrue(manifest['patch']['all_other_bytes_verified_equal'])
        self.assertIn('ignores interval', manifest['scope'])
        self.assertEqual(json.loads((self.out / 'manifest.json').read_text()), manifest)

    def test_complete_build_id_suffix_is_checked(self):
        self.data[0x5f] ^= 1  # Same first sixteen bytes must still fail.
        self.source.write_bytes(self.data)
        self.refused(message='complete NSO BuildId')

    def test_wrong_original_word(self):
        self.data[0x180] ^= 1
        self.source.write_bytes(self.data)
        self.refused(message='original instruction')

    def test_wrong_context_with_matching_word(self):
        self.data[0x140] ^= 1
        self.source.write_bytes(self.data)
        self.refused(message='context SHA256')

    def test_patch_va_outside_text(self):
        self.refused(profile=replace(self.profile, patch_va=0x4200, context_va=0x41c0),
                     message='outside the NSO text')

    def test_context_range_crosses_text_end(self):
        self.refused(profile=replace(self.profile, patch_va=0x41f0, context_va=0x41c0),
                     message='outside the NSO text')

    def test_declared_segment_outside_file(self):
        struct.pack_into('<I', self.data, 0x10, 0x800)
        self.source.write_bytes(self.data)
        self.refused(message='declared NSO segment')

    def test_all_three_compression_flags_refused(self):
        for flag in (1, 2, 4):
            with self.subTest(flag=flag):
                struct.pack_into('<I', self.data, 0xc, flag)
                self.source.write_bytes(self.data)
                self.refused(message='compressed NSO')

    def test_wrong_magic_and_truncated_header(self):
        self.data[:4] = b'NOPE'
        self.source.write_bytes(self.data)
        self.refused(message='NSO0 magic')
        self.source.write_bytes(b'NSO0')
        self.refused(message='truncated NSO')

    def test_source_target_same_path_refused(self):
        before = self.source.read_bytes()
        with self.assertRaisesRegex(MODULE.PreparationError, 'different paths'):
            MODULE.prepare(self.source, self.source, profile=self.profile)
        self.assertEqual(self.source.read_bytes(), before)

    def test_source_is_target_main_refused(self):
        directory = self.local / 'has-original'
        directory.mkdir()
        original = directory / 'main.nso'
        original.write_bytes(self.data)
        with self.assertRaisesRegex(MODULE.PreparationError, 'different paths'):
            MODULE.prepare(original, directory, profile=self.profile)
        self.assertEqual(original.read_bytes(), self.data)

    def test_existing_output_never_overwritten(self):
        self.out.mkdir()
        for name in ('main.nso', 'manifest.json'):
            (self.out / name).write_text('existing user output')
        with self.assertRaisesRegex(MODULE.PreparationError, 'already exists'):
            self.prepare()
        self.assertEqual(self.source.read_bytes(), self.data)
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ['main.nso', 'manifest.json'])
        for path in self.out.iterdir():
            self.assertEqual(path.read_text(), 'existing user output')

    def test_output_cannot_escape_local_through_symlink(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (self.local / 'alias').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(MODULE.PreparationError, 'beneath this checkout'):
            MODULE.prepare(self.source, self.local / 'alias' / 'derived', profile=self.profile)
        self.assertFalse((outside / 'derived').exists())

    def test_exclusive_main_and_manifest_survive_creation_race(self):
        real_open = MODULE.os.open
        for name in ('main.nso', 'manifest.json'):
            with self.subTest(name=name):
                self.out = self.local / ('race-' + name)

                def occupied(path, flags, mode=0o777, *, dir_fd=None):
                    if path == name and dir_fd is not None:
                        self.assertTrue(flags & MODULE.os.O_EXCL)
                        (self.out / name).write_text('concurrent existing output')
                    return real_open(path, flags, mode, dir_fd=dir_fd)

                with patch.object(MODULE.os, 'open', occupied):
                    with self.assertRaises(FileExistsError):
                        self.prepare()
                self.assertEqual((self.out / name).read_text(), 'concurrent existing output')
                self.assertEqual(self.source.read_bytes(), self.data)

    def test_chunk_boundary_copy_and_verification(self):
        with patch.object(MODULE, 'CHUNK_BYTES', 7):
            manifest = self.prepare()
        expected = bytearray(self.data)
        expected[0x180:0x184] = self.profile.replacement_word
        self.assertEqual((self.out / 'main.nso').read_bytes(), expected)
        self.assertTrue(manifest['patch']['all_other_bytes_verified_equal'])

    def test_cli_has_no_profile_or_bypass_argument(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                MODULE.main(['--input', str(self.source), '--out', str(self.out), '--profile', 'other'])
        self.assertEqual(error.exception.code, 2)
        self.assertFalse(self.out.exists())

    def test_cli_uses_fixed_profile_monkeypatched_only_in_fixture(self):
        with patch.object(MODULE, 'PRODUCTION_PROFILE', self.profile):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(MODULE.main(['--input', str(self.source), '--out', str(self.out)]), 0)
        self.assertEqual(self.source.read_bytes(), self.data)


if __name__ == '__main__':
    unittest.main()
