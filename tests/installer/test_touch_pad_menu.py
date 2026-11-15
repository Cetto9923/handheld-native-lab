"""Exercise production menu and drawing methods with counted Android stubs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'


class TouchPadMenu(unittest.TestCase):
    def run_menu(self, restore_bug=False):
        source = SOURCE.read_text()
        start = source.index('        private void openMenu() {')
        end = source.index('        // ---------------- edit mode', start)
        menu = source[start:end]
        if restore_bug:
            menu = menu.replace('clearInput();', 'shown = true; clearInput();')
        start = source.index('        @Override protected void onDraw(Canvas c) {')
        end = source.index('        private void drawCtl(', start)
        drawing = source[start:end]
        fixture = '''
class ViewStub {
    protected void onDraw(Canvas canvas) {}
}
class Canvas {
    void drawRoundRect(Rect rect, float x, float y, Paint paint) {}
    void drawText(String text, float x, float y, Paint paint) {}
}
class Rect {
    float height() { return 1; }
    float centerX() { return 1; }
    float centerY() { return 1; }
}
class Paint {
    void setColor(int color) {}
    void setTextSize(float size) {}
    float getTextSize() { return 1; }
}
class Log {
    static void i(String tag, String message) {}
}
public class TouchPadMenuContract extends ViewStub {
    static final String TAG = "test";
    boolean shown, editing, menuOpen;
    int handleId = -1;
    float u = 1;
    final Object[] ctls = new Object[13];
    final Rect handle = new Rect(), menuBtn = new Rect();
    final Paint fill = new Paint(), line = new Paint(), text = new Paint();
    int controls, menus, clears;
    void invalidate() {}
    void clearInput() { ++clears; }
    void drawCtl(Canvas canvas, int index) { ++controls; }
    void drawPanel(Canvas canvas) {}
    void drawMenu(Canvas canvas) { ++menus; }
''' + menu + drawing + '''
    public static void main(String[] args) {
        TouchPadMenuContract pad = new TouchPadMenuContract();
        Canvas canvas = new Canvas();
        pad.openMenu();
        pad.onDraw(canvas);
        if (pad.shown || !pad.menuOpen || pad.controls != 0 || pad.menus != 1)
            throw new AssertionError("hidden pad expanded or menu disappeared");
        pad.closeMenu();
        if (pad.shown || pad.menuOpen)
            throw new AssertionError("closing menu changed hidden pad");
        pad.shown = true;
        pad.openMenu();
        pad.onDraw(canvas);
        if (!pad.shown || pad.controls != 13 || pad.menus != 2)
            throw new AssertionError("visible pad or menu disappeared");
        pad.closeMenu();
        if (!pad.shown || pad.menuOpen || pad.clears != 2)
            throw new AssertionError("menu close or input clearing failed");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'TouchPadMenuContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True, text=True)
            return subprocess.run(['java', '-cp', temporary, 'TouchPadMenuContract'],
                                  capture_output=True, text=True)

    def test_menu_draws_independently_of_virtual_controls(self):
        result = self.run_menu()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_old_forced_show_is_detected(self):
        result = self.run_menu(restore_bug=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('hidden pad expanded', result.stderr)
