"""Verify the production name loader when optional device files are absent."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'src/android/app/java/org/ffrs/diagnostic'


class UnitNamesContract(unittest.TestCase):
    def test_built_in_names_and_optional_overrides(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.index('    private java.util.Map<Integer, String> unitNames() {')
        end = source.index('\n    /**', start)
        method = source[start:end]
        fixture = '''
package org.ffrs.diagnostic;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Map;
public class UnitNamesContract {
    static final String TAG = "test";
    static class Log { static void w(String tag, String text) {} }
    File directory;
    boolean failDirectory;
    File getExternalFilesDir(Object type) {
        if (failDirectory) throw new IllegalStateException("storage unavailable");
        return directory;
    }
''' + method + '''
    static void requireNames(Map<Integer, String> names) {
        if (names.size() != 8 || !"雷因".equals(names.get(1001)) ||
                !"拉斯韦尔".equals(names.get(1002)))
            throw new AssertionError("built-in names lost");
    }
    public static void main(String[] args) throws Exception {
        UnitNamesContract loader = new UnitNamesContract();
        requireNames(loader.unitNames());
        loader.directory = new File(args[0]);
        requireNames(loader.unitNames());
        File table = new File(loader.directory, "unit-names.txt");
        Files.write(table.toPath(), new byte[0]);
        requireNames(loader.unitNames());
        Files.write(table.toPath(), "bad\\nwrong\\tname\\n1001\\t\\n".getBytes(StandardCharsets.UTF_8));
        requireNames(loader.unitNames());
        Files.write(table.toPath(), "1001\\t覆盖名称\\n2000\\t新增角色\\n".getBytes(StandardCharsets.UTF_8));
        Map<Integer, String> overridden = loader.unitNames();
        if (!"覆盖名称".equals(overridden.get(1001)) ||
                !"拉斯韦尔".equals(overridden.get(1002)) ||
                !"新增角色".equals(overridden.get(2000)))
            throw new AssertionError("override or fallback lost");
        if (!"雷因".equals(UnitNames.name(1001)))
            throw new AssertionError("built-in table was mutated");
        loader.failDirectory = true;
        requireNames(loader.unitNames());
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            code = directory / 'UnitNamesContract.java'
            code.write_text(fixture)
            subprocess.run(['javac', '-d', temporary, str(JAVA / 'UnitNames.java'), str(code)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run(['java', '-cp', temporary, 'org.ffrs.diagnostic.UnitNamesContract',
                                     temporary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
