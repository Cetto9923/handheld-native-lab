"""Exercise the production physical +/- chord without Android timing dependencies."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'


class PadShortcutContract(unittest.TestCase):
    def test_chord_single_keys_repeat_release_and_reset(self):
        source = SOURCE.read_text()
        start = source.index('    private static final class TouchPadShortcut {')
        end = source.index('    private static final class PadShortcutDispatch', start)
        fixture = 'public class ShortcutContract {\n' + source[start:end] + r'''
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) {
        for (int first : new int[]{10, 11}) {
            int second = first == 10 ? 11 : 10;
            TouchPadShortcut pad = new TouchPadShortcut();
            check(!pad.key(first, true, 0));
            check(pad.buttons == 0 && pad.deadline == 160);
            check(pad.key(second, true, 100));
            check(pad.buttons == 0 && pad.deadline == Long.MAX_VALUE);
            check(!pad.key(first, true, 200) && !pad.key(second, true, 250));
            check(!pad.key(first, false, 300));
            check(!pad.key(first, true, 310));
            check(!pad.key(first, false, 320));
            check(!pad.key(second, false, 330));
            pad.flush(1000);
            check(pad.buttons == 0);
            check(!pad.key(first, true, 1100));
            check(pad.key(second, true, 1101));
            pad.reset();
            check(pad.buttons == 0 && pad.held == 0 && pad.pending == 0 && !pad.consumed);

            // Ordinary short taps remain visible to a late game input poll.
            check(!pad.key(first, true, 2000));
            check(!pad.key(first, false, 2040));
            check(pad.buttons == (1L << first));
            pad.flush(2199);check(pad.buttons == (1L << first));
            pad.flush(2200);check(pad.buttons == 0);
            check(!pad.key(first, false, 2300) && pad.buttons == 0);

            // Held keys forward after the chord window and keep their release pulse.
            check(!pad.key(first, true, 3000));
            pad.flush(3159);check(pad.buttons == 0);
            pad.flush(3160);check(pad.buttons == (1L << first));
            check(!pad.key(first, true, 3200));
            check(!pad.key(first, false, 3250));
            pad.flush(3410);check(pad.buttons == 0);

            // A second key outside the window remains an ordinary game input.
            check(!pad.key(first, true, 4000));
            check(!pad.key(second, true, 4160));
            check(pad.buttons == TouchPadShortcut.BOTH);
            check(!pad.key(first, false, 4200));
            check(!pad.key(second, false, 4210));
            pad.flush(4370);check(pad.buttons == 0);

            check(!pad.key(first, true, 5000));
            pad.reset();pad.flush(10000);
            check(pad.buttons == 0 && pad.deadline == Long.MAX_VALUE);
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'ShortcutContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', temporary, 'ShortcutContract'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_normal_entry_points_use_the_shortcut(self):
        source = SOURCE.read_text()
        dispatch = source[source.index('    @Override public boolean dispatchKeyEvent('):
                          source.index('    private static int stick(')]
        self.assertIn('if (bit == 10 || bit == 11)', dispatch)
        self.assertIn('padShortcut.key(bit, event.getAction() == KeyEvent.ACTION_DOWN, now)', dispatch)
        self.assertIn('userToggleTouchPad();', dispatch)
        self.assertIn('padButtons |= 1L << bit', dispatch)
        self.assertIn('padButtons &= ~(1L << bit)', dispatch)
        publish = source[source.index('    private void publishPad()'):source.index('    private void discoverPad()')]
        self.assertIn('touchButtons | padShortcut.buttons', publish)
        pause = source[source.index('    @Override protected void onPause()'):source.index('    @Override public void onCreate(')]
        self.assertIn('padShortcut.reset();', pause)
        self.assertIn('removeCallbacks(padShortcutDispatch)', pause)
