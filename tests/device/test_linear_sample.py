"""Validate float32 bilinear lowering and its persistent shader identity."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'


class LinearSample(unittest.TestCase):
    def test_production_selection(self):
        real = os.environ.get('FFRS_REAL_LINEAR_SAMPLE_MODULE')
        if not real:
            self.skipTest('set FFRS_REAL_LINEAR_SAMPLE_MODULE for the real binding 52 selection')
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('samp_par sampler_parameters(')
        end = source.index('// nvnProgramSetShaders assigns', start)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            header = path / 'production.h'
            header.write_text(source[start:end])
            binary = path / 'runtime'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ENGINE),
                            '-DPRODUCTION_HEADER="' + str(header) + '"',
                            str(Path(__file__).with_name('linear_sample_runtime_contract.cpp')),
                            '-lz', '-o', str(binary)], check=True)
            subprocess.run([str(binary), real], check=True)

    def test_rewrite_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            binary = path / 'contract'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ENGINE),
                            str(Path(__file__).with_name('linear_sample_contract.cpp')),
                            '-o', str(binary)], check=True)
            assembly = Path(__file__).with_name('linear_sample.spvasm').read_text()
            for label, text in [('imported', assembly), ('new_import', assembly.replace(
                    '%glsl = OpExtInstImport "GLSL.std.450"\n', ''))]:
                source = path / (label + '.spvasm')
                source.write_text(text)
                original = source.with_suffix('.spv')
                output = path / (label + '-linear.spv')
                subprocess.run(['spirv-as', '--target-env', 'vulkan1.1', str(source),
                                '-o', str(original)], check=True)
                subprocess.run([str(binary), str(original), str(output), '0'], check=True)
                subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(output)], check=True)
            real = os.environ.get('FFRS_REAL_LINEAR_SAMPLE_MODULE')
            if real:
                for binding in (52, 106, 118):
                    output = path / ('real-' + str(binding) + '.spv')
                    subprocess.run([str(binary), real, str(output), str(binding)], check=True)
                    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(output)], check=True)


if __name__ == '__main__':
    unittest.main()
