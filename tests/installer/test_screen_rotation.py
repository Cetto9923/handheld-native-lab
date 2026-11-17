"""Exercise the production orientation preference and shared game/touch transform."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'src/android/app/java/org/ffrs/diagnostic'


class ScreenRotation(unittest.TestCase):
    def test_orientation_toggle_restore_and_menu_wiring(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.index('    private void restoreScreenOrientation() {')
        end = source.index('    private final TouchPadShortcut', start)
        methods = source[start:end]
        fixture = '''
class ActivityInfo {
    static final int SCREEN_ORIENTATION_LANDSCAPE = 0;
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
    RotationContract(Preferences value) { preferences = value; }
    Preferences getPreferences(int mode) { return preferences; }
    void setRequestedOrientation(int orientation) { requested = orientation; }
''' + methods + '''
    public static void main(String[] args) {
        Preferences preferences = new Preferences();
        RotationContract first = new RotationContract(preferences);
        first.restoreScreenOrientation();
        if (first.requested != 0 || preferences.saved) throw new AssertionError("default");
        first.rootLayout = new FrameLayout();
        first.applyScreenOrientation();
        if (first.rootLayout.rotation != 0f) throw new AssertionError("default transform");
        first.rotateScreen();
        if (first.requested != 0 || !preferences.saved || first.rootLayout.rotation != 180f)
            throw new AssertionError("reverse on orientation-ignoring tablet");
        RotationContract recreated = new RotationContract(preferences);
        recreated.restoreScreenOrientation();
        recreated.rootLayout = new FrameLayout();
        recreated.applyScreenOrientation();
        if (recreated.requested != 0 || recreated.rootLayout.rotation != 180f)
            throw new AssertionError("restore after view creation");
        recreated.rotateScreen();
        if (recreated.requested != 0 || preferences.saved || recreated.rootLayout.rotation != 0f) throw new AssertionError("normal");
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
        self.assertIn('else if (i == 9) owner.toggleFpsOverlay();', source)
        self.assertIn('else owner.rotateScreen();', source)
        self.assertIn('"旋转屏幕180°"', source)
        self.assertIn('private final RectF[] menuItems = new RectF[11];', source)

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
