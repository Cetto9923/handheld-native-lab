"""Exercise the production orientation preference and shared game/touch transform."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'src/android/app/java/org/ffrs/diagnostic'


class ScreenRotation(unittest.TestCase):
    def test_same_surface_resize_publishes_a_fresh_native_window_generation(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.index('    @Override public void surfaceChanged(')
        end = source.index('    @Override public void surfaceDestroyed(', start)
        method = source[start:end].replace('@Override ', '')
        fixture = '''
import java.lang.ref.WeakReference;
class Log { static void i(String tag, String value) {} }
class SurfaceHolder {
    final Object surface = new Object();
    Object getSurface() { return surface; }
}
class GameWindowNative {
    static long generation;
    static long[] setSurface(Object surface) {
        return new long[] {1, 123, 1920, 3000, 1, ++generation};
    }
    static long[] describeWindow() { return new long[] {1, 123, 3000, 1920, 1, generation}; }
}
public class SurfaceResizeContract {
    static final String TAG = "test";
    static WeakReference<SurfaceResizeContract> currentActivity;
    boolean guestStarted = true;
    void startGuest() { throw new AssertionError("running guest restarted"); }
''' + method + '''
    public static void main(String[] args) {
        SurfaceResizeContract activity = new SurfaceResizeContract();
        currentActivity = new WeakReference<>(activity);
        SurfaceHolder holder = new SurfaceHolder();
        activity.surfaceChanged(holder, 1, 3000, 1920);
        activity.surfaceChanged(holder, 1, 1920, 3000);
        activity.surfaceChanged(holder, 1, 3000, 1920);
        if (GameWindowNative.generation != 3)
            throw new AssertionError("same Surface resize left swapchain generation stale");
    }
}
'''
        for restore_bug in (False, True):
            implementation = fixture.replace('GameWindowNative.setSurface(holder.getSurface())',
                                               'GameWindowNative.describeWindow()') if restore_bug else fixture
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                java = path / 'SurfaceResizeContract.java'
                java.write_text(implementation)
                subprocess.run(['javac', str(java)], check=True, capture_output=True)
                result = subprocess.run(['java', '-cp', directory, 'SurfaceResizeContract'],
                                        capture_output=True, text=True)
                if restore_bug:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('swapchain generation stale', result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_presentation_rotates_both_axes_and_preserves_crop_and_borders(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_backend_vulkan.c').read_text()
        start = source.index('    memset(&blit, 0, sizeof(blit));')
        end = source.index('    vkCmdBlitImage(', start)
        fixture = '''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "native_window.h"
#define VK_IMAGE_ASPECT_COLOR_BIT 1
typedef struct { int32_t x, y, z; } Offset;
typedef struct { unsigned aspectMask, layerCount; } Layers;
typedef struct {
    Layers srcSubresource;
    Offset srcOffsets[2];
    Layers dstSubresource;
    Offset dstOffsets[2];
} VkImageBlit;
static VkImageBlit region(int32_t source_x, int32_t source_y, uint32_t source_w,
        uint32_t source_h, int32_t destination_x, int32_t destination_y,
        int32_t destination_w, int32_t destination_h) {
    VkImageBlit blit;
''' + source[start:end] + '''
    return blit;
}
int main(void) {
    assert(!nso_host_native_window_reverse_landscape());
    for (int reverse = 0; reverse < 2; ++reverse) {
        nso_host_native_window_set_reverse_landscape(reverse);
        for (int crop = 0; crop < 2; ++crop) {
            int32_t x = crop ? 17 : 0, y = crop ? 23 : 0;
            VkImageBlit b = region(x, y, 1920, 1080, 0, 116, 3000, 1688);
            assert(b.srcOffsets[0].x == x + (reverse ? 1920 : 0));
            assert(b.srcOffsets[0].y == y + (reverse ? 1080 : 0));
            assert(b.srcOffsets[1].x == x + (reverse ? 0 : 1920));
            assert(b.srcOffsets[1].y == y + (reverse ? 0 : 1080));
            assert(b.srcOffsets[0].z == 0 && b.srcOffsets[1].z == 1);
            assert(b.dstOffsets[0].x == 0 && b.dstOffsets[1].x == 3000);
            assert(b.dstOffsets[0].y == 116 && b.dstOffsets[1].y == 1804);
            assert(b.dstOffsets[0].z == 0 && b.dstOffsets[1].z == 1);
            assert(b.srcSubresource.layerCount == 1 && b.dstSubresource.layerCount == 1);
        }
        nso_host_native_window_clear();
        assert(nso_host_native_window_reverse_landscape() == reverse);
    }
    nso_host_native_window_set_reverse_landscape(false);
    assert(!nso_host_native_window_reverse_landscape());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            implementation = path / 'rotation.c'
            implementation.write_text(fixture)
            command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-pthread',
                       '-I' + str(ROOT / 'src/runtime/hle'),
                       str(implementation), str(ROOT / 'src/runtime/hle/native_window.c'),
                       '-o', str(path / 'rotation')]
            subprocess.run(command, check=True, capture_output=True)
            subprocess.run([str(path / 'rotation')], check=True, capture_output=True)
        bridge = (ROOT / 'src/android/native/game_window_jni.cpp').read_text()
        self.assertIn('Java_org_ffrs_diagnostic_GameWindowNative_setReverseLandscape', bridge)
        self.assertIn('nso_host_native_window_set_reverse_landscape(reverse == JNI_TRUE)', bridge)

    def test_orientation_toggle_restore_and_menu_wiring(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.index('    private void restoreScreenOrientation() {')
        end = source.index('    private final TouchPadShortcut', start)
        methods = source[start:end]
        fixture = '''
class ActivityInfo {
    static final int SCREEN_ORIENTATION_FULL_SENSOR = 10;
    static final int SCREEN_ORIENTATION_REVERSE_LANDSCAPE = 8;
}
class Log { static void i(String tag, String value) {} }
public class RotationContract {
    static final String TAG = "test";
    static final int MODE_PRIVATE = 0;
    static final class Preferences {
        boolean saved;
        boolean getBoolean(String key, boolean fallback) { return saved; }
        Preferences edit() { return this; }
        Preferences putBoolean(String key, boolean value) { saved = value; return this; }
        void apply() {}
    }
    final Preferences preferences;
    boolean reverseLandscape;
    int requested = -1;
    static final class FrameLayout {
        float rotation;
        void setRotation(float value) { rotation = value; }
    }
    FrameLayout rootLayout;
    static final class GameWindowNative {
        static boolean reverse;
        static void setReverseLandscape(boolean value) { reverse = value; }
    }
    RotationContract(Preferences value) { preferences = value; }
    Preferences getPreferences(int mode) { return preferences; }
    void setRequestedOrientation(int orientation) { requested = orientation; }
''' + methods + '''
    public static void main(String[] args) {
        Preferences preferences = new Preferences();
        RotationContract first = new RotationContract(preferences);
        first.restoreScreenOrientation();
        if (first.requested != 10 || preferences.saved) throw new AssertionError("default");
        first.rootLayout = new FrameLayout();
        first.applyScreenOrientation();
        if (first.rootLayout.rotation != 0f) throw new AssertionError("default transform");
        first.rotateScreen();
        if (first.requested != 10 || !preferences.saved || first.rootLayout.rotation != 180f
                || !GameWindowNative.reverse)
            throw new AssertionError("reverse on orientation-ignoring tablet");
        RotationContract recreated = new RotationContract(preferences);
        recreated.restoreScreenOrientation();
        recreated.rootLayout = new FrameLayout();
        recreated.applyScreenOrientation();
        if (recreated.requested != 10 || recreated.rootLayout.rotation != 180f || !GameWindowNative.reverse)
            throw new AssertionError("restore after view creation");
        recreated.rotateScreen();
        if (recreated.requested != 10 || preferences.saved || recreated.rootLayout.rotation != 0f || GameWindowNative.reverse) throw new AssertionError("normal");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'RotationContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True)
            subprocess.run(['java', '-cp', directory, 'RotationContract'], check=True, capture_output=True)
        create = source[source.index('    @Override public void onCreate(Bundle state) {'):]
        self.assertLess(create.index('restoreScreenOrientation();'), create.index('setContentView(root);'))
        self.assertLess(create.index('setContentView(root);'), create.index('applyScreenOrientation();'))
        self.assertIn('root.addView(surface_view,', create)
        self.assertIn('root.addView(touchPad,', create)
        self.assertIn('android:screenOrientation="fullSensor"', (ROOT / 'src/android/app/gamewindow/AndroidManifest.xml').read_text())
        self.assertIn('else if (i == 8) owner.toggleFpsOverlay();', source)
        self.assertIn('else if (i == 9) owner.rotateScreen();', source)
        self.assertIn('else if (i == 10) owner.openDisplaySettings();', source)
        self.assertIn('"旋转屏幕180°"', source)
        self.assertIn('private final RectF[] menuItems = new RectF[12];', source)

    def test_apk_fingerprint_hashes_actual_bytes(self):
        source = (JAVA / 'ReportActivity.java').read_text()
        start = source.index('    static String apkSha256(File file) throws Exception {')
        end = source.index('    private static String metadata(', start)
        fixture = '''import java.io.*;
import java.util.Locale;
import java.nio.file.*;
public class ApkHashContract {
''' + source[start:end] + '''
    public static void main(String[] args) throws Exception {
        File file = new File(args[0]);
        Files.write(file.toPath(), new byte[] {97, 98, 99});
        if (!apkSha256(file).equals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
            throw new AssertionError("APK bytes were not hashed");
        Files.write(file.toPath(), new byte[0]);
        if (!apkSha256(file).equals("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))
            throw new AssertionError("empty hash");
        Files.delete(file.toPath());
        try { apkSha256(file); throw new AssertionError("missing file accepted"); }
        catch (FileNotFoundException expected) {}
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'ApkHashContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True)
            subprocess.run(['java', '-cp', directory, 'ApkHashContract', str(Path(directory) / 'app.apk')],
                           check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
