"""Run the production ADPF lifecycle and API-level fallback without a device."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidFrameHint(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            fixture = path / 'contract.cpp'
            fixture.write_text(source)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT),
                            str(fixture), '-o', str(path / 'contract')], check=True)
            subprocess.run([str(path / 'contract')], check=True)

    def test_lifecycle_and_duration(self):
        source = (ROOT / 'tests/device/android_frame_hint_contract.cpp').read_text()
        self.compile_run(source.replace('../../src/', 'src/'))

    def test_api_level_and_missing_symbols(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('XbgFrameHintApi android_frame_hint_api() {')
        body = source[start:source.index('XbgFrameHint android_frame_hint(', start)]
        fixture = r'''
#define __ANDROID__ 1
#include "src/graphics/nvn_vulkan/engine/xbg_android_frame_hint.h"
#include <cassert>
#include <cstring>
#define RTLD_DEFAULT nullptr
static int sdk = 32, symbol_calls = 0;
int android_get_device_api_level() { return sdk; }
void *dlsym(void *, const char *name) {
    ++symbol_calls;
    assert(!std::strncmp(name,"APerformanceHint_",17));
    return nullptr;
}
''' + body + r'''
int main() {
    auto legacy = android_frame_hint_api();
    assert(!legacy.manager && symbol_calls == 0);
    sdk = 33;
    auto missing = android_frame_hint_api();
    assert(!missing.manager && symbol_calls == 5);
    XbgFrameHint hints(missing);
    hints.finish_frame(true,1,33,100,0);
    assert(hints.unavailable() && !hints.active());
}
'''
        self.compile_run(fixture)
        self.assertIn('{"androidhint",&android_frame_hint_enabled}', source)
        self.assertIn('xbg_report_frame_hint(timing_clock(),pace_ns,s_target_frame_ns)', source)
        self.assertIn('lock(mutex);android_frame_hint.reset();if(!initialized)', source)


if __name__ == '__main__':
    unittest.main()
