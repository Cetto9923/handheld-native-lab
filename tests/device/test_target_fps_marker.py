#!/usr/bin/env python3
"""Run the actual native limiter configuration reader with temporary markers."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TargetFpsMarker(unittest.TestCase):
    def test_marker_precedence_and_defaults(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('static uint64_t s_target_frame_ns =')
        end = source.index('static uint64_t xbg_frame_pace()', start)
        body = source[start:end]
        fixture = r'''#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <unistd.h>
static std::string parent;
static std::string pipeline_cache_path() { return parent + "/vk-pipeline-cache.bin"; }
static void xbg_logf(const char *, ...) {}
''' + body + r'''
static void marker(const char *value) {
    FILE *f = fopen((parent + "/gfx-target-fps").c_str(), "wb");
    if (!f) std::abort();
    fputs(value, f);
    fclose(f);
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    parent = argv[1];
    (void)s_next_present_ns;
    (void)s_pace_check_counter;
    unsetenv("FFRS_TARGET_FPS");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 33333333ull) return 2;
    setenv("FFRS_TARGET_FPS", "60", 1);
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 16666666ull) return 3;
    marker("30\n");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 33333333ull) return 4;
    marker("not-a-number\n");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 16666666ull) return 5;
    marker("0\n");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 0) return 6;
    unlink((parent + "/gfx-target-fps").c_str());
    unsetenv("FFRS_TARGET_FPS");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 33333333ull) return 7;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            env = os.environ.copy()
            env.pop('FFRS_TARGET_FPS', None)
            subprocess.run([str(path / 'test'), str(path)], env=env, check=True)


if __name__ == '__main__':
    unittest.main()
