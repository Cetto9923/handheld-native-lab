#!/usr/bin/env python3
"""Run the real depth-aspect marker resolver with temporary marker files.

The Honor tablet (Adreno 735) must not reuse the open depth/stencil pass for
a depth sample, because that path flickers the moon halo in the real scene.
The marker file is the switch: "1" forces the reuse on, "0" forces it off and
"auto" keeps the device default, which is off on the Honor tablet and on
everywhere else.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'
APP = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'
INSTALLER = ROOT / 'src/android/app/java/org/ffrs/diagnostic/DataBundleInstaller.java'


def resolver_source():
    source = ENGINE.read_text()
    start = source.index('static void xbg_resolve_depth_aspect(')
    end = source.index('\n}\n', start) + len('\n}\n')
    return source[start:end]


class DepthAspectMarker(unittest.TestCase):
    def test_marker_precedence_and_defaults(self):
        fixture = r"""#include <cstdio>
#include <cstring>
#include <string>
static bool depth_aspect_enabled=false;
static unsigned long long depth_aspect_kept=0;
static const char *depth_aspect_source="device default";
""" + resolver_source() + r"""
static bool write_marker(const char *dir, const char *value) {
    FILE *f = fopen((std::string(dir) + "/gfx-depth-aspect").c_str(), "wb");
    if (!f) return false;
    fputs(value, f);
    fclose(f);
    return true;
}
static std::string read_marker(const char *dir) {
    FILE *f = fopen((std::string(dir) + "/gfx-depth-aspect").c_str(), "rb");
    if (!f) return std::string();
    char buf[32] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return std::string(buf, n);
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const char *dir = argv[1];
    // No marker at all: the device default decides.
    xbg_resolve_depth_aspect(dir, true);
    if (depth_aspect_enabled) return 2;
    xbg_resolve_depth_aspect(dir, false);
    if (!depth_aspect_enabled) return 3;
    // "auto" keeps the device default.
    if (!write_marker(dir, "auto\n")) return 4;
    xbg_resolve_depth_aspect(dir, true);
    if (depth_aspect_enabled) return 5;
    xbg_resolve_depth_aspect(dir, false);
    if (!depth_aspect_enabled) return 6;
    // "0" forces the reuse off, "1" forces it on.
    if (!write_marker(dir, "0\n")) return 7;
    xbg_resolve_depth_aspect(dir, true);
    if (depth_aspect_enabled) return 8;
    xbg_resolve_depth_aspect(dir, false);
    if (depth_aspect_enabled) return 9;
    if (!write_marker(dir, "1")) return 10;
    xbg_resolve_depth_aspect(dir, true);
    if (!depth_aspect_enabled) return 11;
    if (read_marker(dir) != "1") return 12;
    // The app's old default ("1\n") is migrated to the neutral token, so it
    // stops forcing the path on where the path flickers.
    if (!write_marker(dir, "1\n")) return 13;
    xbg_resolve_depth_aspect(dir, true);
    if (depth_aspect_enabled) return 14;
    if (read_marker(dir) != "auto\n") return 15;
    xbg_resolve_depth_aspect(dir, false);
    if (!depth_aspect_enabled) return 16;
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test'), str(path)], check=True)

    def test_app_writes_the_neutral_default(self):
        # The app must not force the reuse on: the engine owns the device
        # decision, and only the player may override it.
        pattern = re.compile(r'gfx-depth-aspect"\)?\s*,\s*"([^"]*)"')
        for name, path in (('GameActivity', APP), ('DataBundleInstaller', INSTALLER)):
            found = pattern.search(path.read_text())
            self.assertTrue(found is not None, name + ': no gfx-depth-aspect marker write')
            self.assertEqual(found.group(1), 'auto\\n', name + ': the app must not force the reuse on')


if __name__ == '__main__':
    unittest.main()
