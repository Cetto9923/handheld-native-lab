"""Execute the production probe request poll with counted file operations."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProbeMarkerPoll(unittest.TestCase):
    def test_overlay_request_can_be_armed_after_loading(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('bool partial_overlay_available=false;')
        body = source[start:source.index('void histogram_sync_rewrite(', start)]
        self.assertIn('partial_overlay_available=honor;', source)
        self.assertIn('partial_overlay_diagnostic=read_partial_overlay_request(overlay);', source)
        submit = source[source.index('static void xbg_submit_now('):]
        self.assertLess(submit.index('if(pr>=live_next){live_next=pr+30;'),
                        submit.index('poll_partial_overlay_request(probe.base,pr);'))
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
unsigned opens=0,reads=0,closes=0,changes=0;
int value='0'; bool available=true;
FILE *test_fopen(const char *path,const char *mode) {
    assert(!std::strcmp(path,"/test/gfx-partial-overlay-check") && !std::strcmp(mode,"rb"));
    ++opens; return available?reinterpret_cast<FILE *>(uintptr_t(1)):nullptr;
}
int test_fgetc(FILE *file) { assert(file); ++reads; return value; }
int test_fclose(FILE *file) { assert(file); ++closes; return 0; }
void xbg_logf(const char *, ...) { ++changes; }
#define fopen test_fopen
#define fgetc test_fgetc
#define fclose test_fclose
''' + body + r'''
int main() {
    for(unsigned frame=0;frame<1000;++frame)poll_partial_overlay_request("/test",frame);
    assert(opens==0 && partial_overlay_diagnostic==0);
    partial_overlay_available=true;
    poll_partial_overlay_request("/test",1000);
    assert(opens==1 && reads==1 && closes==1 && changes==0);
    value='5';poll_partial_overlay_request("/test",1030);
    assert(partial_overlay_diagnostic==5 && changes==1);
    poll_partial_overlay_request("/test",1060);assert(changes==1);
    value='0';poll_partial_overlay_request("/test",1090);
    assert(partial_overlay_diagnostic==0 && changes==2);
    for(value='1';value<='5';++value){
        poll_partial_overlay_request("/test",1120);
        assert(partial_overlay_diagnostic==unsigned(value-'0'));
    }
    assert(changes==7);
    value=-1;poll_partial_overlay_request("/test",1150);
    assert(partial_overlay_diagnostic==0 && changes==8);
    value='6';poll_partial_overlay_request("/test",1180);assert(changes==8);
    value='4';poll_partial_overlay_request("/test",1210);
    unsigned previous_closes=closes;
    available=false;poll_partial_overlay_request("/test",1240);
    assert(partial_overlay_diagnostic==0 && closes==previous_closes);
    assert(changes==10 && reads==closes && opens==reads+1);
    assert(read_partial_overlay_request(nullptr)==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

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
