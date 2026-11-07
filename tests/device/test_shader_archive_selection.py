#!/usr/bin/env python3
"""Exercise the JNI archive selection body against real files and markers."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ShaderArchiveSelection(unittest.TestCase):
    def test_portable_archive_and_explicit_override(self):
        source = (ROOT / 'src/android/native/guest_boot_jni.cpp').read_text()
        start = source.index('        unsetenv("FFRS_SHADER_ARCHIVE");')
        end = source.index('        nvn_engine_set_shader_directory', start)
        body = source[start:end]
        fixture = r'''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
struct Request { std::string image; };
static void select_archive(const std::string &root) {
    Request req{root + "/main.nso"}; Request *request = &req;
    const std::string shaders = root + "/shaders";
''' + body + r'''
}
static void write(const std::string &path, const char *value) {
    FILE *f = fopen(path.c_str(), "wb"); if (!f) std::abort();
    fputs(value, f); fclose(f);
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const std::string root = argv[1], archive = root + "/shaders.hnla";
    const std::string marker = root + "/gfx-shader-archive";
    setenv("FFRS_SHADER_ARCHIVE", "stale", 1);
    select_archive(root); if (getenv("FFRS_SHADER_ARCHIVE")) return 2;
    write(archive, ""); select_archive(root);
    if (getenv("FFRS_SHADER_ARCHIVE")) return 3;
    write(archive, "fixture"); select_archive(root);
    if (!getenv("FFRS_SHADER_ARCHIVE") || archive != getenv("FFRS_SHADER_ARCHIVE")) return 4;
    write(marker, "0"); select_archive(root);
    if (getenv("FFRS_SHADER_ARCHIVE")) return 5;
    write(marker, "1"); select_archive(root);
    if (!getenv("FFRS_SHADER_ARCHIVE")) return 6;
    unlink(marker.c_str()); unlink(archive.c_str()); mkdir(archive.c_str(), 0700);
    select_archive(root); if (getenv("FFRS_SHADER_ARCHIVE")) return 7;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test'), tmp], check=True)


if __name__ == '__main__':
    unittest.main()
