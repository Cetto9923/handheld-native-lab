"""Execute the production opt-in ADPF adapter against counted API calls."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PerformanceHint(unittest.TestCase):
    def test_real_ndk_minimum_sdk_and_function_types(self):
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        if not ndk:
            installed = sorted((Path.home() / 'Library/Android/sdk/ndk').glob('*'))
            if not installed:
                self.skipTest('Android NDK is unavailable for the cross-compile check')
            ndk = installed[-1]
        bins = list((Path(ndk) / 'toolchains/llvm/prebuilt').glob('*/bin'))
        self.assertTrue(bins)
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('namespace performance_hint {')
        adapter = source[start:source.index('void timing_start(){', start)]
        smoke = r'''
#include <android/performance_hint.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdint>
#include <type_traits>
uint64_t timing_clock();
void xbg_logf(const char *, ...);
''' + adapter + r'''
#if __ANDROID_API__ >= 33
static_assert(std::is_same<decltype(&APerformanceHint_getManager), decltype(performance_hint::get_manager)>::value);
static_assert(std::is_same<decltype(&APerformanceHint_createSession), decltype(performance_hint::create_session)>::value);
static_assert(std::is_same<decltype(&APerformanceHint_closeSession), decltype(performance_hint::close_session)>::value);
static_assert(std::is_same<decltype(&APerformanceHint_reportActualWorkDuration), decltype(performance_hint::report_work)>::value);
#endif
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'adapter.cpp').write_text(smoke)
            for api in (28, 33):
                compiler = bins[0] / ('aarch64-linux-android' + str(api) + '-clang++')
                subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fPIC', '-c', str(path / 'adapter.cpp'),
                                '-o', str(path / ('adapter' + str(api) + '.o'))], check=True)

    def test_optional_api_timing_and_session_lifetime(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('namespace performance_hint {')
        adapter = source[start:source.index('void timing_start(){', start)]
        self.assertIn('bool enabled=false;', adapter)
        self.assertIn('{"adpf",&performance_hint::enabled}', source)
        submit = source[source.index('static void xbg_submit_now('):]
        self.assertLess(submit.index('LIVE-SET'), submit.index('performance_hint::begin_work();'))
        present = source[source.index('static void xbg_present_texture_now('):]
        self.assertLess(present.index('performance_hint::finish_work();'),
                        present.index('uint64_t pace_ns=xbg_frame_pace();'))
        shutdown = source[source.index('extern "C" int xbg_game_shutdown()'):]
        self.assertLess(shutdown.index('gfx_async_stop();'), shutdown.index('performance_hint::reset();'))
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
struct APerformanceHintManager {} manager;
struct APerformanceHintSession {} hint_session;
bool missing_library = false, missing_symbol = false;
bool missing_manager = false, reject_session = false;
int report_result = 0, current_tid = 71;
unsigned library_opens = 0, library_closes = 0, creates = 0, closes = 0, calls = 0;
int64_t reported = 0;
uint64_t now = 100;
uint64_t timing_clock() { return now; }
int gettid() { return current_tid; }
void xbg_logf(const char *, ...) {}
APerformanceHintManager *APerformanceHint_getManager() {
    return missing_manager ? nullptr : &manager;
}
APerformanceHintSession *APerformanceHint_createSession(
        APerformanceHintManager *m, const int32_t *tids, size_t count, int64_t target) {
    assert(m == &manager && count == 1 && tids[0] == current_tid && target == 33333333);
    if (reject_session) return nullptr;
    ++creates; return &hint_session;
}
void APerformanceHint_closeSession(APerformanceHintSession *s) {
    assert(s == &hint_session && closes < creates); ++closes;
}
int APerformanceHint_reportActualWorkDuration(APerformanceHintSession *s, int64_t duration) {
    assert(s == &hint_session && duration > 0);
    ++calls; reported = duration; return report_result;
}
enum { RTLD_NOW = 1, RTLD_LOCAL = 2 };
void *dlopen(const char *name, int flags) {
    assert(!std::strcmp(name, "libandroid.so") && flags == 3);
    if (missing_library) return nullptr;
    ++library_opens; return &manager;
}
int dlclose(void *handle) {
    assert(handle == &manager && library_closes < library_opens);
    ++library_closes; return 0;
}
void *dlsym(void *handle, const char *name) {
    assert(handle == &manager);
    if (!std::strcmp(name, "APerformanceHint_getManager"))
        return reinterpret_cast<void *>(&APerformanceHint_getManager);
    if (!std::strcmp(name, "APerformanceHint_createSession"))
        return reinterpret_cast<void *>(&APerformanceHint_createSession);
    if (!std::strcmp(name, "APerformanceHint_closeSession"))
        return reinterpret_cast<void *>(&APerformanceHint_closeSession);
    assert(!std::strcmp(name, "APerformanceHint_reportActualWorkDuration"));
    return missing_symbol ? nullptr : reinterpret_cast<void *>(&APerformanceHint_reportActualWorkDuration);
}
''' + adapter + r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    const int test = std::atoi(argv[1]);
    performance_hint::begin_work(); performance_hint::finish_work();
    assert(library_opens == 0 && creates == 0 && calls == 0);
    performance_hint::enabled = true;
#ifndef __ANDROID__
    (void)test;
    performance_hint::begin_work(); performance_hint::finish_work(); performance_hint::reset();
    assert(library_opens == 0 && creates == 0 && calls == 0);
    return 0;
#else
    if (test >= 1 && test <= 4) {
        missing_library = test == 1; missing_symbol = test == 2;
        missing_manager = test == 3; reject_session = test == 4;
        for (int i = 0; i < 1000; ++i) {
            performance_hint::begin_work(); performance_hint::finish_work();
        }
        assert(performance_hint::blocked && creates == 0 && calls == 0);
    } else {
        performance_hint::begin_work();
        assert(creates == 1 && performance_hint::owner == current_tid);
        now += 1000;
        performance_hint::begin_work(); // Additional submits belong to the same frame.
        now += 25000000;
        performance_hint::finish_work();
        assert(calls == 1 && reported == 25001000);
        now += 100000000; // Frame pacing / idle after finish must not enter the report.
        performance_hint::finish_work(); assert(calls == 1);
        performance_hint::begin_work(); now += 20000000;
        if (test == 5) report_result = 32;
        performance_hint::finish_work(); assert(calls == 2 && reported == 20000000);
        if (test == 5) {
            assert(performance_hint::blocked && closes == 1);
            performance_hint::begin_work(); assert(creates == 1);
        } else {
            assert(test == 0);
            ++current_tid; // A new long-lived rendering thread gets its own session.
            performance_hint::begin_work();
            assert(creates == 2 && closes == 1 && performance_hint::owner == current_tid);
            now += 15000000; performance_hint::finish_work();
            assert(calls == 3 && reported == 15000000);
            performance_hint::begin_work(); // Nonmonotonic/empty measurements are ignored.
            performance_hint::finish_work(); assert(calls == 3);
        }
    }
    performance_hint::enabled = false;
    performance_hint::begin_work(); performance_hint::finish_work(); performance_hint::reset();
    assert(closes == creates && library_closes == library_opens);
    assert(!performance_hint::session && !performance_hint::library && !performance_hint::blocked);
    missing_library = missing_symbol = missing_manager = reject_session = false;
    report_result = 0;
    // Retrying after an explicit toggle and repeated renderer restarts stays bounded.
    for (unsigned i = 0; i < 1000; ++i) {
        performance_hint::enabled = true;
        performance_hint::begin_work(); now += 10; performance_hint::finish_work();
        performance_hint::reset();
        assert(closes == creates && library_closes == library_opens);
    }
#endif
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            for defines in ([], ['-D__ANDROID__']):
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', *defines, str(path / 'test.cpp'),
                                '-o', str(path / 'test')], check=True)
                for case in range(6):
                    subprocess.run([str(path / 'test'), str(case)], check=True)


if __name__ == '__main__':
    unittest.main()
