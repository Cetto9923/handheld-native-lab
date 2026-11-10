"""Exercise actual index CLI using self-authored local files only."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/distribution/index_pipeline_warm_list.py'
KEY = b'0123456789abcdef'


class SeedIndex(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / '.local', prefix='seed-index-test-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.index = self.base / 'index'
        self.index.mkdir()
        self.archive = self.base / 'shaders.hnla'
        self.archive.write_bytes(struct.pack('<8sII16sQQ', b'HNLXGS1\0', 1, 0, KEY, 48, 32) + b'x' * 32)
        main = self.base / 'main'; main.write_bytes(b'self-authored fixture, not game data')
        rows = [dict(path=name, source=str(path), bytes=path.stat().st_size,
                     sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                for name, path in [('main.nso', main), ('shaders.hnla', self.archive)]]
        raw = ''.join(f"{r['sha256']}\t{r['bytes']}\t{r['path']}\n" for r in rows).encode()
        record = dict(format='hnl-native-data/1', abi='arm64-v8a', files=rows,
                      file_count=2, payload_bytes=sum(r['bytes'] for r in rows),
                      index_sha256=hashlib.sha256(raw).hexdigest(),
                      required={r['path']: r['sha256'] for r in rows})
        (self.index / 'files.tsv').write_bytes(raw)
        (self.index / 'index.json').write_text(json.dumps(record))
        self.original = (self.index / 'index.json').read_bytes()
        words = [1, 0, 0, 0, 0, 1, 0, 16, 0x2e171b5f, 0xbfad94ed]
        self.seed = self.base / 'seed.bin'
        self.seed.write_bytes(struct.pack('<III', 0x57504c58, 1, 16) + KEY
                             + struct.pack('<I10I', 10, *words))

    def run_index(self, output):
        return subprocess.run(['python3', str(SCRIPT), '--index', str(self.index),
                               '--warm-list', str(self.seed), '--out', str(output)],
                              capture_output=True, text=True)

    def test_new_contract_pins_seed_and_preserves_every_original_row(self):
        output = self.base / 'result'
        result = self.run_index(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        old = json.loads(self.original)
        new = json.loads((output / 'index.json').read_text())
        self.assertEqual(new['file_count'], 3)
        for row in old['files']:
            self.assertIn(row, new['files'])
        self.assertEqual(new['required']['pipeline-warm-list.bin'],
                         hashlib.sha256(self.seed.read_bytes()).hexdigest())
        self.assertEqual(hashlib.sha256((output / 'files.tsv').read_bytes()).hexdigest(),
                         new['index_sha256'])
        self.assertEqual((self.index / 'index.json').read_bytes(), self.original)
        self.assertFalse(any('cache.bin' in row['path'] for row in new['files']))

    def test_bad_shader_identity_does_not_create_output(self):
        raw = bytearray(self.seed.read_bytes()); raw[-1] ^= 1; self.seed.write_bytes(raw)
        output = self.base / 'result'
        self.assertNotEqual(self.run_index(output).returncode, 0)
        self.assertFalse(output.exists())

    def test_missing_shader_and_mutated_source_index_are_refused(self):
        self.archive.write_bytes(struct.pack('<8sII16sQQ', b'HNLXGS1\0', 1, 0,
                                             b'ffffffffffffffff', 48, 32) + b'x' * 32)
        output = self.base / 'result'
        self.assertNotEqual(self.run_index(output).returncode, 0)
        self.assertFalse(output.exists())
        (self.index / 'files.tsv').write_bytes(b'changed')
        self.assertNotEqual(self.run_index(output).returncode, 0)
        self.assertFalse(output.exists())

    def test_existing_output_and_linked_seed_are_preserved(self):
        output = self.base / 'result'; output.mkdir()
        keep = output / 'keep'; keep.write_bytes(b'keep')
        self.assertNotEqual(self.run_index(output).returncode, 0)
        self.assertEqual(keep.read_bytes(), b'keep')
        moved = self.base / 'real-seed'; self.seed.rename(moved)
        self.seed.symlink_to(moved)
        output = self.base / 'new-result'
        self.assertNotEqual(self.run_index(output).returncode, 0)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
