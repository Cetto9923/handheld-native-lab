"""Exercise the portable folder packager with self-authored fixtures only.

The packager carries the learned pipeline recipes next to main.nso. These
checks use a fixture archive and a fixture warm list; no game data is read and
no device is touched.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/distribution/package_honor_portable.py'
KEY = b'0123456789abcdef'
SEED = 'pipeline-warm-list.bin'


class PortablePackaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / '.local', prefix='portable-package-test-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.data = self.base / 'data'
        self.data.mkdir()
        (self.data / 'main.nso').write_bytes(b'self-authored fixture, not game data')
        (self.data / 'sdk.nso').write_bytes(b'self-authored fixture, not game data')
        (self.data / 'shaders.hnla').write_bytes(
            struct.pack('<8sII16sQQ', b'HNLXGS1\0', 1, 0, KEY, 48, 32) + b'x' * 32)
        shaders = self.data / 'shaders'
        shaders.mkdir()
        (shaders / '0123456789abcdef.xgs').write_bytes(b'self-authored fixture, not game data')
        romfs = self.data / 'romfs'
        romfs.mkdir()
        (romfs / 'fixture.bin').write_bytes(b'self-authored fixture, not game data')
        self.words = [1, 0, 0, 0, 0, 1, 0, 16, 0x2e171b5f, 0xbfad94ed]
        self.seed_bytes = (struct.pack('<III', 0x57504c58, 1, 16) + KEY
                           + struct.pack('<I10I', 10, *self.words))

    def write_seed(self, payload=None):
        (self.data / SEED).write_bytes(self.seed_bytes if payload is None else payload)

    def run_pack(self, name='out'):
        out = self.base / name
        result = subprocess.run(['python3', str(SCRIPT), '--data', str(self.data),
                                  '--out', str(out)], capture_output=True, text=True)
        return result, out

    def test_folder_without_a_seed_is_still_packaged(self):
        result, out = self.run_pack()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(out / 'FFRS-data.zip') as archive:
            names = archive.namelist()
        self.assertIn('FFRS/main.nso', names)
        self.assertNotIn('FFRS/' + SEED, names)
        manifest = json.loads((out / 'data-manifest.json').read_text())
        self.assertNotIn('pipeline_warm_seed', manifest)

    def test_the_seed_entity_travels_with_the_folder(self):
        self.write_seed()
        result, out = self.run_pack()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(out / 'FFRS-data.zip') as archive:
            names = archive.namelist()
            self.assertIn('FFRS/' + SEED, names)
            self.assertEqual(archive.read('FFRS/' + SEED), self.seed_bytes)
        manifest = json.loads((out / 'data-manifest.json').read_text())
        seed = manifest['pipeline_warm_seed']
        self.assertEqual(seed['path'], 'FFRS/' + SEED)
        self.assertEqual(seed['records'], 1)
        self.assertEqual(seed['unique_records'], 1)
        self.assertEqual(seed['missing_shader_keys'], [])
        self.assertEqual(seed['bytes'], len(self.seed_bytes))

    def test_an_unexpected_file_is_refused(self):
        (self.data / 'GameSaveData0').write_bytes(b'save data is never packaged')
        result, out = self.run_pack()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(out.exists())

    def test_a_corrupt_seed_is_refused(self):
        broken = bytearray(self.seed_bytes)
        broken[-1] ^= 1
        self.write_seed(bytes(broken))
        result, out = self.run_pack()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(out.exists())

    def test_a_seed_with_an_unknown_shader_is_refused(self):
        self.write_seed(struct.pack('<III', 0x57504c58, 1, 16) + b'ffffffffffffffff'
                       + struct.pack('<I10I', 10, *self.words))
        result, out = self.run_pack()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(out.exists())

    def test_a_linked_seed_is_refused(self):
        real = self.base / 'real-seed'
        real.write_bytes(self.seed_bytes)
        (self.data / SEED).symlink_to(real)
        result, out = self.run_pack()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(out.exists())


class SeedAddition(unittest.TestCase):
    """The learned recipes can be added to a packaged folder that already
    exists. The payload is not rewritten and the addition can be undone."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / '.local', prefix='portable-add-test-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.data = self.base / 'data'
        self.data.mkdir()
        (self.data / 'main.nso').write_bytes(b'self-authored fixture, not game data')
        (self.data / 'sdk.nso').write_bytes(b'self-authored fixture, not game data')
        (self.data / 'shaders.hnla').write_bytes(
            struct.pack('<8sII16sQQ', b'HNLXGS1\0', 1, 0, KEY, 48, 32) + b'x' * 32)
        shaders = self.data / 'shaders'
        shaders.mkdir()
        (shaders / '0123456789abcdef.xgs').write_bytes(b'self-authored fixture, not game data')
        romfs = self.data / 'romfs'
        romfs.mkdir()
        (romfs / 'fixture.bin').write_bytes(b'self-authored fixture, not game data')
        self.words = [1, 0, 0, 0, 0, 1, 0, 16, 0x2e171b5f, 0xbfad94ed]
        self.seed_bytes = (struct.pack('<III', 0x57504c58, 1, 16) + KEY
                           + struct.pack('<I10I', 10, *self.words))
        self.seed = self.base / 'pipeline-warm-list.bin'
        self.seed.write_bytes(self.seed_bytes)
        self.zip = self.base / 'FFRS-data.zip'
        with zipfile.ZipFile(self.zip, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as z:
            for path in sorted(p for p in self.data.rglob('*') if p.is_file()):
                name = 'FFRS/' + path.relative_to(self.data).as_posix()
                with z.open(name, 'w', force_zip64=True) as dst:
                    dst.write(path.read_bytes())
        self.before = self.entry_state()
        self.before_bytes = self.zip.stat().st_size
        self.before_sha = hashlib.sha256(self.zip.read_bytes()).hexdigest()

    def entry_state(self):
        with zipfile.ZipFile(self.zip) as archive:
            return [(i.filename, i.header_offset, i.CRC, i.file_size, i.compress_size)
                    for i in archive.infolist()]

    def run_add(self, seed=None, archive=None):
        return subprocess.run(
            ['python3', str(SCRIPT), '--add-to', str(archive or self.zip),
             '--seed', str(seed or self.seed)],
            capture_output=True, text=True)

    def test_the_seed_is_added_and_the_payload_is_untouched(self):
        result = self.run_add()
        self.assertEqual(result.returncode, 0, result.stderr)
        after = self.entry_state()
        self.assertEqual(after[:len(self.before)], self.before)
        self.assertEqual([e[0] for e in after[len(self.before):]], ['FFRS/pipeline-warm-list.bin'])
        with zipfile.ZipFile(self.zip) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('FFRS/pipeline-warm-list.bin'), self.seed_bytes)
        block = json.loads((self.base / 'FFRS-data.zip.prewarm-seed.json').read_text())
        self.assertEqual(block['entries_before'], len(self.before))
        self.assertEqual(block['entries_after'], len(self.before) + 1)
        self.assertEqual(block['sha256_before'], self.before_sha)
        self.assertEqual(block['pipeline_warm_seed']['records'], 1)
        self.assertEqual(block['pipeline_warm_seed']['missing_shader_keys'], [])
        # The payload was not rewritten: only the new entry, its headers and
        # the directory were written, so the file grew by the seed and a few
        # hundred bytes, not by the size of the folder.
        self.assertLess(self.zip.stat().st_size - self.before_bytes,
                        len(self.seed_bytes) + 4096)

    def test_a_second_addition_is_refused(self):
        self.assertEqual(self.run_add().returncode, 0)
        result = self.run_add()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already carries', result.stderr)

    def test_a_seed_with_an_unknown_shader_is_refused(self):
        broken = self.base / 'broken.bin'
        broken.write_bytes(struct.pack('<III', 0x57504c58, 1, 16) + b'ffffffffffffffff'
                          + struct.pack('<I10I', 10, *self.words))
        result = self.run_add(seed=broken)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.entry_state(), self.before)

    def test_the_addition_can_be_undone_byte_for_byte(self):
        self.assertEqual(self.run_add().returncode, 0)
        result = subprocess.run(['python3', str(SCRIPT), '--restore', str(self.zip)],
                                 capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.entry_state(), self.before)
        self.assertEqual(hashlib.sha256(self.zip.read_bytes()).hexdigest(), self.before_sha)


    def test_the_delivery_checksums_follow_the_folder(self):
        delivery = self.base / 'delivery'
        delivery.mkdir()
        with zipfile.ZipFile(self.zip) as archive:
            rows = [{'path': i.filename, 'bytes': i.file_size} for i in archive.infolist()]
        (delivery / 'data-manifest.json').write_text(json.dumps(
            {'format': 'ffrs-portable-folder-v1', 'files': rows,
             'file_count': len(rows), 'zip_bytes': self.before_bytes,
             'zip_sha256': self.before_sha}, indent=2))
        (delivery / 'FFRS-data.zip.sha256').write_text(self.before_sha + '  FFRS-data.zip\n')
        result = subprocess.run(
            ['python3', str(SCRIPT), '--add-to', str(self.zip), '--seed', str(self.seed),
             '--delivery', str(delivery)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((delivery / 'data-manifest.json').read_text())
        self.assertEqual(manifest['file_count'], len(rows) + 1)
        self.assertEqual(manifest['pipeline_warm_seed']['records'], 1)
        self.assertEqual(manifest['pipeline_warm_seed']['missing_shader_keys'], [])
        digest = hashlib.sha256(self.zip.read_bytes()).hexdigest()
        self.assertEqual(manifest['zip_sha256'], digest)
        self.assertEqual((delivery / 'FFRS-data.zip.sha256').read_text(),
                         digest + '  FFRS-data.zip\n')
        paths = [row['path'] for row in manifest['files']]
        self.assertEqual(paths[-1], 'FFRS/pipeline-warm-list.bin')

    def test_a_zip_without_a_shader_archive_is_refused(self):
        bare = self.base / 'bare.zip'
        with zipfile.ZipFile(bare, 'w', compression=zipfile.ZIP_STORED) as z:
            z.writestr('FFRS/main.nso', b'self-authored fixture, not game data')
        result = self.run_add(archive=bare)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('shader archive', result.stderr)



if __name__ == '__main__':
    unittest.main()
