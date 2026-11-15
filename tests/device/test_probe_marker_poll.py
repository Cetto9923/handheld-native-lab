"""Execute the production probe request poll with counted file operations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProbeMarkerPoll(unittest.TestCase):
    def test_frame_cache_and_live_request(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_probe.inc').read_text()
        start = source.index('uint64_t probe_marker_frame = UINT64_MAX;')
        body = source[start:source.index('void probe_image_read_barrier(', start)]
        fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct { std::atomic<uint64_t> presents{0}; } stats;
struct { std::string base = "/test"; } probe;
static int opens, reads, closes, request = '0';
static bool available = true, force = false;
bool ab_off(const char *name) { assert(!std::strcmp(name,"probepoll")); return force; }
FILE *test_fopen(const char *path, const char *mode) {
    assert(!std::strcmp(path,"/test/gfx-codex-probe") && !std::strcmp(mode,"rb"));
    ++opens;
    return available ? reinterpret_cast<FILE *>(uintptr_t(1)) : nullptr;
}
int test_fgetc(FILE *file) { assert(file); ++reads; return request; }
int test_fclose(FILE *file) { assert(file); ++closes; return 0; }
#define fopen test_fopen
#define fgetc test_fgetc
#define fclose test_fclose
''' + body + r'''
int main() {
    for(int i=0;i<1000;++i) assert(probe_marker_value() == '0');
    assert(opens == 1 && reads == 1 && closes == 1);
    request = '3';
    assert(probe_marker_value() == '0'); // A request becomes visible next frame.
    ++stats.presents;
    assert(probe_marker_value() == '3' && opens == 2);
    request = '1';
    ++stats.presents;
    assert(probe_marker_value() == '1' && opens == 3);
    request = -1;
    ++stats.presents;
    assert(probe_marker_value() == -1); // An empty staged request remains unarmed.
    available = false;
    ++stats.presents;
    assert(probe_marker_value() == '0' && opens == 5 && reads == 4 && closes == 4);
    available = true;
    request = 'R';
    ++stats.presents;
    assert(probe_marker_value() == 'R');
    force = true;
    request = '0';
    for(int i=0;i<10;++i) assert(probe_marker_value() == '0');
    assert(opens == 16 && reads == 15 && closes == 15);
    force = false;
    assert(probe_marker_value() == '0' && opens == 16);
    // A new engine session can reset its frame counter to zero.
    stats.presents = 0;
    probe_marker_frame = UINT64_MAX;
    probe_marker_cached_value = '0';
    request = 'D';
    assert(probe_marker_value() == 'D' && opens == 17);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)
        for name in ['probe_image_read_barrier', 'probe_depth_decision',
                     'probe_guest_barrier', 'probe_cpu_sync', 'probe_present']:
            start = source.index('void ' + name + '(')
            end = source.find('\nvoid ', start + 1)
            function = source[start:end if end >= 0 else len(source)]
            self.assertIn('probe_marker_value()', function)
            self.assertNotIn('FILE *file=fopen', function)
        self.assertIn("probe_marker_frame=UINT64_MAX;probe_marker_cached_value='0';", source)


if __name__ == '__main__':
    unittest.main()
