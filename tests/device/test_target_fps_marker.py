#!/usr/bin/env python3
"""Run the actual native limiter configuration reader with temporary markers."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TargetFpsMarker(unittest.TestCase):
    def test_existing_install_migration_and_display_reader(self):
        source = (ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java').read_text()
        start = source.index('    private static void migrateDefaultFramePacing(')
        migration = source[start:source.index('    private void ensureDefaultMarkers()', start)]
        start = source.index('    private static void writeMarkerIfMissing(')
        writer = source[start:source.index('    private float requestedDisplayHz', start)]
        start = source.index('    private int requestedGameFps()')
        reader = source[start:source.index('    private static void migrateDefaultFramePacing(', start)]
        fixture = r'''
import java.io.File;
import java.nio.file.Files;
public class PacingMigration {
    static final String TAG = "test";
    static class Log { static void w(String tag, String message, Exception error) {} }
    File directory;
    File getExternalFilesDir(Object ignored) { return directory; }
''' + migration + writer + reader + r'''
    public static void main(String[] args) throws Exception {
        File root = new File(args[0]);
        for (String initial : new String[] {"missing", "30", "60", "45", "0"}) {
            File dir = new File(root, initial);
            if (!dir.mkdir()) throw new AssertionError("mkdir");
            File target = new File(dir, "gfx-target-fps");
            if (!initial.equals("missing")) Files.write(target.toPath(), (initial + "\n").getBytes());
            migrateDefaultFramePacing(dir);
            String expected = initial.equals("45") ? "45" : "0";
            if (!new String(Files.readAllBytes(target.toPath())).trim().equals(expected))
                throw new AssertionError("migration " + initial);
            PacingMigration activity = new PacingMigration();
            activity.directory = dir;
            if (activity.requestedGameFps() != Integer.parseInt(expected))
                throw new AssertionError("display disagrees with limiter");
            Files.write(target.toPath(), "30\n".getBytes());
            migrateDefaultFramePacing(dir);
            if (activity.requestedGameFps() != 30) throw new AssertionError("later explicit cap");
        }
        File blocked = new File(root, "blocked");
        if (!blocked.mkdir() || !new File(blocked, "gfx-target-fps").mkdir())
            throw new AssertionError("blocked fixture");
        migrateDefaultFramePacing(blocked);
        if (new File(blocked, "gfx-unlocked-default-v1").exists())
            throw new AssertionError("failed write must retry next start");
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'PacingMigration.java').write_text(fixture)
            subprocess.run(['javac', '-d', str(path), str(path / 'PacingMigration.java')], check=True)
            subprocess.run(['java', '-cp', str(path), 'PacingMigration', str(path)], check=True)

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
    if (s_target_frame_ns != 0) return 2;
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
    if (s_target_frame_ns != 0) return 7;
    setenv("FFRS_RENDER_POLICY", "1", 1);
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 0) return 8;
    marker("45\n");
    xbg_refresh_target_fps();
    if (s_target_frame_ns != 22222222ull) return 9;
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
