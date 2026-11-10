#!/usr/bin/env python3
"""Exercise the actual JNI profile environment body, not Android rendering."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProfileEnvironment(unittest.TestCase):
    def test_profile_switch_and_external_override(self):
        source = (ROOT / 'src/android/native/guest_boot_jni.cpp').read_text()
        signature = 'Java_org_ffrs_diagnostic_GameWindowNative_setProfileTargetFps(JNIEnv*, jclass, jint fps, jboolean docked) {'
        start = source.index(signature) + len(signature)
        end = source.index('\n}\n', start)
        body = source[start:end]
        fixture = '''#include <cstdlib>\n#include <cstring>\n#include <cstdio>\n'''
        fixture += 'static void apply(int fps, bool docked) {' + body + '\n}\n'
        fixture += r'''
static bool eq(const char *name, const char *wanted) {
    const char *value = getenv(name);
    return wanted ? value && strcmp(value, wanted) == 0 : value == nullptr;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "default")) {
        apply(30, false);
        if (!eq("FFRS_TARGET_FPS", nullptr) || !eq("FFRS_PROFILE_TABLET", "0") ||
            !eq("FFRS_PROFILE_OPERATION_MODE", "0")) return 3;
        apply(60, true);
        if (!eq("FFRS_TARGET_FPS", "60") || !eq("FFRS_PROFILE_TABLET", "1") ||
            !eq("FFRS_PROFILE_OPERATION_MODE", "1")) return 4;
        apply(30, false);
        if (!eq("FFRS_TARGET_FPS", nullptr) || !eq("FFRS_PROFILE_TABLET", "0") ||
            !eq("FFRS_PROFILE_OPERATION_MODE", "0")) return 5;
    } else if (!strcmp(argv[1], "external")) {
        apply(60, true);
        if (!eq("FFRS_TARGET_FPS", "40")) return 6;
        apply(30, false);
        if (!eq("FFRS_TARGET_FPS", "40") || !eq("FFRS_PROFILE_TABLET", "0")) return 7;
    } else return 8;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            env = os.environ.copy()
            for key in ('FFRS_TARGET_FPS', 'FFRS_PROFILE_TABLET', 'FFRS_PROFILE_OPERATION_MODE'):
                env.pop(key, None)
            subprocess.run([str(path / 'test'), 'default'], env=env, check=True)
            env['FFRS_TARGET_FPS'] = '40'
            subprocess.run([str(path / 'test'), 'external'], env=env, check=True)


    def test_surface_transform_keeps_handheld_path(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_backend_vulkan.c').read_text()
        start = source.index('    const char *tablet_profile = getenv("FFRS_PROFILE_TABLET");')
        end = source.index('    fprintf(stderr, "[nvn] surface transform', start)
        body = source[start:end]
        fixture = r"""#include <stdlib.h>
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 1u
struct Capabilities { unsigned supportedTransforms; unsigned currentTransform; };
struct Swapchain { unsigned preTransform; };
static unsigned select_transform(unsigned current, unsigned supported) {
    struct Capabilities capabilities = {supported, current};
    struct Swapchain swapchain_info = {0};
""" + body + r"""
    return swapchain_info.preTransform;
}
int main(void) {
    unsetenv("FFRS_PROFILE_TABLET");
    if (select_transform(4, 0x1ff) != 4) return 1;
    setenv("FFRS_PROFILE_TABLET", "0", 1);
    if (select_transform(4, 0x1ff) != 4) return 2;
    setenv("FFRS_PROFILE_TABLET", "1", 1);
    if (select_transform(4, 0x1ff) != 1) return 3;
    if (select_transform(4, 4) != 4) return 4;
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                            '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


    def test_inline_gate_is_profile_and_selected_adreno_only(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        anchor = source.index('vkGetPhysicalDeviceProperties(cfg.physical,&properties)')
        start = source.index('#ifdef XBG_HAVE_SPIRV_OPT\n', anchor) + len('#ifdef XBG_HAVE_SPIRV_OPT\n')
        end = source.index('\n#endif', start)
        body = source[start:end]
        fixture = r"""#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
struct Properties { unsigned vendorID; const char *deviceName; } properties;
struct Config { const char *shader_directory; } cfg;
static bool honor_inline_enabled = false;
static void xbg_logf(const char *, ...) {}
static bool decide(const char *profile, unsigned vendor, const char *device, const char *directory) {
    setenv("FFRS_PROFILE_TABLET", profile, 1);
    properties = {vendor, device};
    cfg.shader_directory = directory;
""" + body + r"""
    return honor_inline_enabled;
}
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    const std::string shaders = std::string(argv[1]) + "/shaders";
    if (decide("0", 0x5143, "Adreno (TM) 735", shaders.c_str())) return 2;
    if (decide("1", 0x13b5, "Mali-G615", shaders.c_str())) return 3;
    if (!decide("1", 0x5143, "Adreno (TM) 735", shaders.c_str())) return 5;
    if (!decide("1", 0x5143, "Adreno (TM) 750", shaders.c_str())) return 8;
    if (!decide("1", 0x5143, "Adreno (TM) 830", shaders.c_str())) return 4;
    if (decide("0", 0x5143, "Adreno (TM) 750", shaders.c_str())) return 9;
    if (decide("0", 0x5143, "Adreno (TM) 830", shaders.c_str())) return 10;
    FILE *marker = fopen((std::string(argv[1]) + "/gfx-honor-inline").c_str(), "wb");
    if (!marker) return 6;
    fputs("0", marker); fclose(marker);
    if (decide("1", 0x5143, "Adreno (TM) 735", shaders.c_str())) return 7;
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test'), str(path)], check=True)


if __name__ == '__main__':
    unittest.main()
