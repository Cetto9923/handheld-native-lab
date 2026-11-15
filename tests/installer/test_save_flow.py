"""Exercise production backup/restore outcomes and atomic file replacement."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'


class SaveFlowContract(unittest.TestCase):
    def test_isolated_seed_copies_all_files_and_protects_existing_progress(self):
        source = SOURCE.read_text()
        start = source.index('    private static void seedRegressionSave(')
        seed = source[start:source.index('    /** The save slot', start)]
        start = source.index('    private static void copyFile(')
        copy = source[start:source.index('    /** Removes a folder', start)]
        fixture = 'package org.ffrs.diagnostic;\nimport java.io.*;\nimport java.nio.file.*;\nimport java.util.*;\n' + \
            'public class RegressionSeedContract {\nstatic final String TAG="test";\n' + \
            'static class Log { static void i(String tag,String message) {} }\n' + seed + copy + r'''
    static void check(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        File base = new File(args[0]), original = new File(base, "original"),
             destination = new File(base, "copy"), empty = new File(base, "empty"),
             other = new File(base, "other");
        check(original.mkdir() && destination.mkdir() && empty.mkdir() && other.mkdir());
        String[] names = {"GameSaveData0", "GameSaveData1", "GameSaveData2", "GameSaveData3",
                          "GameSaveData4", "GameSaveData5", "OptionSaveData",
                          "EnhancedInputUserSettings", "GameSaveData0.orig"};
        for (String name : names) Files.write(new File(original, name).toPath(), name.getBytes("UTF-8"));
        seedRegressionSave(original, destination);
        check(destination.listFiles().length == names.length);
        for (String name : names) {
            byte[] expected = name.getBytes("UTF-8");
            check(Arrays.equals(expected, Files.readAllBytes(new File(original, name).toPath())));
            check(Arrays.equals(expected, Files.readAllBytes(new File(destination, name).toPath())));
        }
        try { seedRegressionSave(original, destination); throw new AssertionError(); }
        catch (IOException expected) {}
        try { seedRegressionSave(original, original); throw new AssertionError(); }
        catch (IOException expected) {}
        try { seedRegressionSave(empty, other); throw new AssertionError(); }
        catch (IOException expected) {}
        check(other.listFiles().length == 0);
    }
}
'''
        self.assertIn('if (isolatedSave && getIntent().getBooleanExtra("isolated_regression_seed", false))', source)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'RegressionSeedContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', '-d', temporary, str(path),
                            str(SOURCE.parent / 'NativeDataBundle.java')],
                           check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', temporary, 'org.ffrs.diagnostic.RegressionSeedContract', temporary],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_custom_values_expand_only_on_explicit_action(self):
        source = SOURCE.read_text()
        start = source.index('    private static final class CheatDetails ')
        end = source.index('    private void showFlowMessage(', start)
        listener = source[start:end]
        fixture = '''
class View {
    static final int VISIBLE = 0, GONE = 8;
    interface OnClickListener { void onClick(View view); }
    int visibility = GONE;
    int getVisibility() { return visibility; }
    void setVisibility(int value) { visibility = value; }
}
class Button extends View {
    String text;
    void setText(String text) { this.text = text; }
}
public class CheatDetailsContract {
''' + listener + '''
    public static void main(String[] args) {
        View custom = new View();
        Button button = new Button();
        CheatDetails action = new CheatDetails(custom);
        action.onClick(button);
        if (custom.visibility != View.VISIBLE || !button.text.contains("收起"))
            throw new AssertionError("expand failed");
        action.onClick(button);
        if (custom.visibility != View.GONE || button.text.contains("收起"))
            throw new AssertionError("collapse failed");
    }
}
'''
        self.assertIn('custom.setVisibility(View.GONE);', source)
        with tempfile.TemporaryDirectory() as temporary:
            code = Path(temporary) / 'CheatDetailsContract.java'
            code.write_text(fixture)
            subprocess.run(['javac', str(code)], check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', temporary, 'CheatDetailsContract'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_backup_restore_and_failure_feedback(self):
        source = SOURCE.read_text()

        def method(signature):
            start = source.index(signature)
            return source[start:source.index('\n    /**', start)]

        methods = method('    void quickSave() {')
        methods += method('    private File nextSnapshotDir() {')
        methods += method('    private java.util.List<File> snapshots() {')
        methods += method('    void loadSnapshot(File snap) {')
        methods += method('    private static void copyFile(File from, File to)')
        methods = methods.replace('private static void copyFile(', 'private static void copyFileImpl(')
        fixture = '''
import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;
public class SaveFlowContract {
    static final String TAG = "test", RELOAD_STEPS = "读取存档";
    static class Log {
        static void i(String tag, String message) {}
        static void w(String tag, String message) {}
    }
    static class Dialog { void dismiss() {} }
    Dialog saveCardsDialog;
    File liveSaveRoot, backupRoot;
    String title, message;
    static int copies, failAt;
    File quickSaveDir() { return backupRoot; }
    void showFlowMessage(String title, String message) { this.title = title; this.message = message; }
    void captureThumbnail(File file) {}
    String timeStamp(long time) { return Long.toString(time); }
    String snapshotNote(File file) { return "test metadata"; }
    void writeText(File file, String text) throws java.io.IOException {
        Files.write(file.toPath(), text.getBytes("UTF-8"));
    }
    static void copyFile(File from, File to) throws java.io.IOException {
        if (++copies == failAt) throw new java.io.IOException("injected storage failure");
        copyFileImpl(from, to);
    }
''' + methods + '''
    static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        File base = new File(args[0]);
        SaveFlowContract app = new SaveFlowContract();
        app.backupRoot = new File(base, "backups");
        check(app.backupRoot.mkdir(), "backup root");
        app.quickSave();
        check("备份未完成".equals(app.title) && app.snapshots().isEmpty(), "null root");
        app.liveSaveRoot = new File(base, "live");
        check(app.liveSaveRoot.mkdir(), "live root");
        app.quickSave();
        check("备份未完成".equals(app.title) && app.snapshots().isEmpty(), "empty root");
        File live = new File(app.liveSaveRoot, "GameSaveData0");
        byte[] original = {1, 2, 3, 4};
        Files.write(live.toPath(), original);
        Files.write(new File(app.liveSaveRoot, "GameSaveData0.orig").toPath(), new byte[]{7});
        app.quickSave();
        check("存档已备份".equals(app.title), "success message");
        List<File> saved = app.snapshots();
        check(saved.size() == 1, "published snapshot");
        check(Arrays.equals(original, Files.readAllBytes(new File(saved.get(0), live.getName()).toPath())), "saved bytes");
        check(!new File(saved.get(0), "GameSaveData0.orig").exists(), "original backup excluded");
        failAt = copies + 1;
        app.quickSave();
        check("备份未完成".equals(app.title) && app.snapshots().size() == 1, "failed backup hidden");
        failAt = 0;
        Files.write(live.toPath(), new byte[]{9});
        app.loadSnapshot(saved.get(0));
        check("备份已恢复，请读档生效".equals(app.title) && app.message.contains(RELOAD_STEPS), "reload guidance");
        check(Arrays.equals(original, Files.readAllBytes(live.toPath())), "restored bytes");
        failAt = copies + 1;
        Files.write(live.toPath(), new byte[]{8});
        app.loadSnapshot(saved.get(0));
        check("恢复未完成".equals(app.title), "restore failure reported");
        check(Arrays.equals(new byte[]{8}, Files.readAllBytes(live.toPath())), "failed target retained");
        failAt = 0;
        File invalid = new File(base, "empty");
        check(invalid.mkdir(), "empty backup");
        app.loadSnapshot(invalid);
        check("恢复未完成".equals(app.title), "empty backup refused");
        copyFileImpl(new File(saved.get(0), live.getName()), live);
        check(Arrays.equals(original, Files.readAllBytes(live.toPath())), "atomic replacement");
        try {
            copyFileImpl(new File(base, "missing-source"), live);
            throw new AssertionError("missing source accepted");
        } catch (java.io.IOException expected) {}
        check(Arrays.equals(original, Files.readAllBytes(live.toPath())), "copy failure retained target");
        File[] temporary = app.liveSaveRoot.listFiles((dir, name) -> name.startsWith(".ffrs-copy-"));
        check(temporary != null && temporary.length == 0, "copy stage cleaned");
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'SaveFlowContract.java'
            path.write_text(fixture)
            subprocess.run(['javac', str(path)], check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', temporary, 'SaveFlowContract', temporary],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
