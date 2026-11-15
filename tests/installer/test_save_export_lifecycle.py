"""Exercise the production onStop body with a lifecycle and export counter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'


class SaveExportLifecycle(unittest.TestCase):
    def run_lifecycle(self, automatic_export=False):
        source = SOURCE.read_text()
        start = source.index('    @Override protected void onStop() {')
        end = source.index('\n    @Override protected void onDestroy()', start)
        method = source[start:end]
        if automatic_export:
            method = method.replace('super.onStop();', 'exportSaves(); super.onStop();')
        fixture = '''
class ActivityStub {
    int stops;
    protected void onStop() { ++stops; }
}
public class SaveExportLifecycleContract extends ActivityStub {
    static final String TAG = "test";
    static class Log { static void i(String tag, String message) {} }
    Object liveSaveRoot = new Object();
    int exports;
    private void exportSaves() { ++exports; }
''' + method + '''
    public static void main(String[] args) {
        SaveExportLifecycleContract activity = new SaveExportLifecycleContract();
        for (int i = 0; i < 3; ++i) activity.onStop();
        if (activity.stops != 3 || activity.exports != 0)
            throw new AssertionError("background transitions exported saves");
        activity.exportSaves();
        if (activity.exports != 1) throw new AssertionError("manual export unavailable");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'SaveExportLifecycleContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True, text=True)
            return subprocess.run(['java', '-cp', directory, 'SaveExportLifecycleContract'],
                                  capture_output=True, text=True)

    def test_background_transitions_leave_export_to_explicit_action(self):
        result = self.run_lifecycle()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('if (i == 0) owner.exportSaves();', SOURCE.read_text())

    def test_regression_reintroducing_automatic_export_is_detected(self):
        result = self.run_lifecycle(automatic_export=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('background transitions exported saves', result.stderr)
