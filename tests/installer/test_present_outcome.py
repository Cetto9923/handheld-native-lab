"""Compile the actual engine adapter body with a controlled backend callback.
No GPU or device presentation is simulated as an acceptance result.
"""
import os
import csv
import io
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PresentOutcome(unittest.TestCase):
    def test_actual_trace_writer_preserves_all_three_outcomes(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('struct FrameTrace {')
        body = source[start:source.index('\n// All access', start)]
        fixture = r'''#include <atomic>
#include <cstdio>
#include <cstdint>
#include <ctime>
struct {
    std::atomic<uint64_t> presents{0}, draws{3}, dispatches{4}, submissions{5};
} stats;
''' + body + r'''
int main() {
    frame_trace.file = stdout;
    for (int result : {0,1,-1}) {
        ++stats.presents;
        frame_trace_record(7, 9, result);
    }
    return 0;
}
'''
        # GCC needs initializer_list explicitly; keep this fixture portable.
        fixture = '#include <initializer_list>\n' + fixture
        with tempfile.TemporaryDirectory() as temp:
            code = Path(temp) / 'writer.cpp'
            binary = Path(temp) / 'writer'
            code.write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(code), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            rows = list(csv.reader(io.StringIO(result.stdout)))
            self.assertEqual(len(rows), 3)
            self.assertTrue(all(len(r) == 8 for r in rows))
            self.assertEqual([int(r[-1]) for r in rows], [0, 1, -1])
            self.assertEqual([int(r[0]) for r in rows], [1, 2, 3])
            self.assertTrue(all(r[2:7] == ['7', '9', '3', '4', '5'] for r in rows))

    def test_actual_engine_adapter_does_not_turn_drop_into_success(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_engine.c').read_text()
        start = source.index('static int engine_present(')
        body = source[start:source.index('\nstatic void engine_log', start)]
        fixture = r'''#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "src/graphics/nvn_device/nvn_device.h"
typedef void *VkImage;
typedef uint32_t VkFormat;
''' + body + r'''
static int outcome, calls;
static nvn_device_state device;
static int backend_present(nvn_device_state *d, void *image, uint32_t format,
    uint32_t width, uint32_t height, const int32_t crop[4], int32_t interval,
    char *reason, size_t reason_size) {
    assert(d == &device && image == (void *)0x123 && format == 37);
    assert(width == 1280 && height == 720 && crop[2] == 1280 && interval == 1);
    assert(reason && reason_size == 256); ++calls; return outcome;
}
int main(void) {
    nvn_backend backend = {.present_image = backend_present};
    const int32_t crop[4] = {0,0,1280,720};
    device.backend = &backend;
    outcome = NVN_PRESENT_QUEUED;
    assert(engine_present(&device, NULL, (void *)0x123, 37, 1280, 720, crop, 1) == 0);
    outcome = NVN_PRESENT_SKIPPED;
    assert(engine_present(&device, NULL, (void *)0x123, 37, 1280, 720, crop, 1) == 1);
    outcome = NVN_PRESENT_FAILED;
    assert(engine_present(&device, NULL, (void *)0x123, 37, 1280, 720, crop, 1) == -1);
    assert(calls == 3);
    backend.present_image = NULL;
    assert(engine_present(&device, NULL, NULL, 0, 0, 0, NULL, 0) == -1);
    device.backend = NULL;
    assert(engine_present(&device, NULL, NULL, 0, 0, 0, NULL, 0) == -1);
    assert(engine_present(NULL, NULL, NULL, 0, 0, 0, NULL, 0) == -1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            code = Path(temp) / 'contract.c'
            binary = Path(temp) / 'contract'
            code.write_text(fixture)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(ROOT), str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
