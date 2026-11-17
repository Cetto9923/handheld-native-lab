"""Exercise memory policy and the production GPU cache-reclaim ordering."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'


class MemoryPressure(unittest.TestCase):
    def compile(self, source, binary, *extra):
        result = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                 '-fsanitize=address,undefined', '-I', str(ENGINE),
                                 *extra, str(source), '-o', str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_policy_and_live_snapshot_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            status, meminfo = path / 'status', path / 'meminfo'
            status.write_text('Name: game\nVmRSS: 3072000 kB\n')
            meminfo.write_text('MemTotal: 12000000 kB\nMemAvailable: 400000 kB\n')
            binary = path / 'policy'
            source = (ENGINE / 'xbg_game.cpp').read_text()
            start = source.index('extern "C" void xbg_request_memory_trim(')
            end = source.index('extern "C" int xbg_game_shutdown()', start)
            header = path / 'request.h'
            header.write_text(source[start:end])
            self.compile(Path(__file__).with_name('memory_pressure_contract.cpp'), binary,
                         '-DREQUEST_HEADER="' + str(header) + '"')
            subprocess.run([str(binary), str(status), str(meminfo)], check=True)

    def run_reclaim(self, mutation=None):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('void reclaim_memory_caches(')
        end = source.index('void maintain_memory_pressure()', start)
        production = source[start:end]
        if mutation:
            before, after = mutation
            self.assertIn(before, production)
            production = production.replace(before, after, 1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            header = path / 'production.h'
            header.write_text(production)
            binary = path / 'reclaim'
            self.compile(Path(__file__).with_name('memory_reclaim_contract.cpp'), binary,
                         '-DPRODUCTION_HEADER="' + str(header) + '"')
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_production_reclaim(self):
        self.assertEqual(self.run_reclaim().returncode, 0)

    def test_gpu_authority_negative_control(self):
        self.assertEqual(self.run_reclaim(('image.gpu_written || ', '')).returncode, 9)

    def test_fence_order_negative_control(self):
        self.assertNotEqual(self.run_reclaim(('complete_all();', '')).returncode, 0)

    def test_active_submission_negative_control(self):
        condition = ('slot.pending || slot.recording || slot.waiters.n.load() || '
                     '!slot.copy_readers.empty()')
        self.assertNotEqual(self.run_reclaim((condition, 'slot.pending && false')).returncode, 0)

    def run_warm(self, remove_worker_guard=False):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('size_t trim_for_memory_pressure(')
        end = source.index('// 首次启动还没有列表', start)
        lifetime_start = source.index('struct WorkerLifetime {')
        lifetime_end = source.index('};', lifetime_start) + 2
        production = source[lifetime_start:lifetime_end] + '\n' + source[start:end]
        if remove_worker_guard:
            condition = r'gpu_idle\s*&&\s*active_workers.load\(\)\s*==\s*0'
            self.assertRegex(production, condition)
            production = re.sub(condition, 'gpu_idle', production)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            header = path / 'production.h'
            header.write_text(production)
            binary = path / 'warm'
            self.compile(Path(__file__).with_name('memory_warm_contract.cpp'), binary,
                         '-DPRODUCTION_HEADER="' + str(header) + '"')
            return subprocess.run([str(binary)], capture_output=True, text=True)

    def test_warm_worker_ownership(self):
        self.assertEqual(self.run_warm().returncode, 0)

    def test_warm_worker_negative_control(self):
        self.assertEqual(self.run_warm(remove_worker_guard=True).returncode, 12)


if __name__ == '__main__':
    unittest.main()
