"""Exercise production menu and drawing methods with counted Android stubs."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'


class TouchPadMenu(unittest.TestCase):
    def run_menu(self, restore_bug=False, restore_large_fps=False):
        source = SOURCE.read_text()
        start = source.index('        private void openMenu() {')
        end = source.index('        // ---------------- edit mode', start)
        menu = source[start:end]
        if restore_bug:
            menu = menu.replace('clearInput();', 'shown = true; clearInput();')
        start = source.index('        @Override protected void onDraw(Canvas c) {')
        end = source.index('        private void drawCtl(', start)
        drawing = source[start:end]
        if restore_large_fps:
            drawing = drawing.replace('text.setTextSize(0.26f * u);',
                                      'text.setTextSize(0.62f * u);')
        fixture = '''
class ViewStub {
    protected void onDraw(Canvas canvas) {}
}
class Canvas {
    int fpsDraws;
    float fpsSize;
    void drawRoundRect(Rect rect, float x, float y, Paint paint) {}
    void drawRoundRect(float l, float t, float r, float b, float x, float y, Paint paint) {}
    void drawText(String text, float x, float y, Paint paint) {
        if (text.startsWith("FPS")) { ++fpsDraws; fpsSize = paint.getTextSize(); }
    }
}
class Rect {
    float height() { return 1; }
    float centerX() { return 1; }
    float centerY() { return 1; }
}
class Paint {
    float size;
    enum Align { LEFT, CENTER }
    void setTextAlign(Align align) {}
    void setColor(int color) {}
    void setTextSize(float value) { size = value; }
    float getTextSize() { return size; }
    float measureText(String value) { return value.length() * size * 0.6f; }
}
class Log {
    static void i(String tag, String message) {}
}
public class TouchPadMenuContract extends ViewStub {
    static class Owner {
        boolean fpsVisible;
        String fpsLabel = "FPS 30.0";
    }
    final Owner owner = new Owner();
    static final String TAG = "test";
    boolean shown, editing, menuOpen;
    int handleId = -1;
    float u = 1;
    static class Ctl { boolean visible = true; }
    final Ctl[] ctls = new Ctl[@CONTROL_COUNT@];
    TouchPadMenuContract() { for (int i=0; i<ctls.length; i++) ctls[i]=new Ctl(); }
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
        if (!pad.shown || pad.controls != @CONTROL_COUNT@ || pad.menus != 2)
            throw new AssertionError("visible pad or menu disappeared");
        pad.closeMenu();
        if (!pad.shown || pad.menuOpen || pad.clears != 2)
            throw new AssertionError("menu close or input clearing failed");
        pad.shown = false;
        pad.owner.fpsVisible = true;
        pad.onDraw(canvas);
        if (canvas.fpsDraws != 1 || pad.controls != @CONTROL_COUNT@)
            throw new AssertionError("FPS overlay depends on virtual pad visibility");
        if (canvas.fpsSize > 0.30f * pad.u || canvas.fpsSize <= 0)
            throw new AssertionError("FPS overlay is too large");
    }
}
'''
        count = re.search(r'ctls = new Ctl\[(\d+)\]', source)[1]
        fixture = fixture.replace('@CONTROL_COUNT@', count)
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

    def test_old_large_fps_is_detected(self):
        result = self.run_menu(restore_large_fps=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('FPS overlay is too large', result.stderr)

    def test_two_column_menu_fits_and_has_distinct_hit_targets(self):
        source = SOURCE.read_text()
        start = source.index('            float mw = ')
        end = source.index('\n        }', start)
        geometry = source[start:end]
        fixture = '''
class RectF {
    float left, top, right, bottom;
    void set(float l, float t, float r, float b) {
        left = l; top = t; right = r; bottom = b;
    }
}
public class MenuGeometryContract {
    float u;
    RectF menuPanel = new RectF();
    RectF[] menuItems = new RectF[@MENU_COUNT@];
    void layout(int w, int h) {
        u = Math.min(w, h) / 15f;
        for (int i = 0; i < menuItems.length; ++i) menuItems[i] = new RectF();
''' + geometry + '''
    }
    public static void main(String[] args) {
        int[][] sizes = {{3000,1920}, {1334,750}, {1920,1080}, {1776,1200}, {720,1280}};
        for (int[] size : sizes) {
            MenuGeometryContract menu = new MenuGeometryContract();
            menu.layout(size[0], size[1]);
            RectF panel = menu.menuPanel;
            if (panel.left < 0 || panel.top < 0 || panel.right > size[0] || panel.bottom > size[1])
                throw new AssertionError("panel exceeds viewport");
            for (int i = 0; i < menu.menuItems.length; ++i) {
                RectF r = menu.menuItems[i];
                if (r.left < panel.left || r.right > panel.right || r.top < panel.top ||
                        r.bottom > panel.bottom - 0.7f * menu.u ||
                        r.right <= r.left || r.bottom - r.top < 0.9f * menu.u)
                    throw new AssertionError("menu item clipped or too small " + i);
                RectF first = menu.menuItems[i % 2];
                if (r.left != first.left || r.right != first.right)
                    throw new AssertionError("columns are not aligned");
                if (i % 2 == 1 && r.top != menu.menuItems[i-1].top)
                    throw new AssertionError("row is not aligned");
                for (int j = 0; j < i; ++j) {
                    RectF other = menu.menuItems[j];
                    if (r.left < other.right && r.right > other.left &&
                            r.top < other.bottom && r.bottom > other.top)
                        throw new AssertionError("overlapping hit targets");
                }
            }
        }
    }
}
'''
        count = re.search(r'menuItems = new RectF\[(\d+)\]', source)[1]
        fixture = fixture.replace('@MENU_COUNT@', count)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'MenuGeometryContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', directory, 'MenuGeometryContract'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
