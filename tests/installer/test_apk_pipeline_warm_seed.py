"""Exercise APK seed staging with pure-Java, self-authored warm-list fixtures."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / "src/android/app/java/org/ffrs/diagnostic"


class ApkPipelineWarmSeed(unittest.TestCase):
    def test_transactional_staging_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            classes = Path(temporary) / "classes"
            classes.mkdir()
            subprocess.run(["javac", "--release", "8", "-d", str(classes),
                            str(JAVA / "NativeDataBundle.java"),
                            str(ROOT / "tests/installer/PipelineWarmSeedContract.java")], check=True)
            result = subprocess.run(["java", "-cp", str(classes), "PipelineWarmSeedContract",
                                     str(Path(temporary) / "fixtures")],
                                    check=True, capture_output=True, text=True)
            self.assertIn("pipeline seed staging contracts passed=", result.stdout)

    def test_background_startup_uses_selected_runtime(self):
        source = (JAVA / "GameActivity.java").read_text()
        startup = source[source.index("private void startGuest()"):
                         source.index("private static final class GuestStartChanged")]
        selected = "File files = managed ? dataInstaller.runtimeRoot : getExternalFilesDir(null);"
        launch = "new GuestBootTask(image, romfs, log_path, saveRoot, getAssets(), files, guestArgs)"
        self.assertLess(startup.index("GUEST_START.begin()"), startup.index(selected))
        self.assertLess(startup.index(selected), startup.index(launch))
        task = startup[startup.index("@Override public void run()") :]
        stage = "NativeDataBundle.stageApkPipelineWarmSeed(seed, runtime);"
        self.assertIn('try (java.io.InputStream seed = assets.open("pipeline-warm-list.bin"))', task)
        self.assertLess(task.index(stage), task.index("GameWindowNative.nativeStartGuest"))
        self.assertLess(task.index(stage), task.index("GameWindowNative.nativeStartInstalledGuest"))
        self.assertIn("catch (java.io.FileNotFoundException missing)", task)
        self.assertIn("catch (java.io.IOException | SecurityException error)", task)
        self.assertIn("GUEST_START.finish(thread != -1);", task)

    def test_render_percentage_arguments(self):
        source = (JAVA / "GameActivity.java").read_text()
        method = re.search(r"    static String\[\] renderPercentageArguments\(int percentage\) \{.*?\n    \}",
                           source, re.DOTALL)
        self.assertIsNotNone(method)
        harness = "public class RenderArguments {\n" + method.group(0) + "\n" + r'''
    public static void main(String[] args) {
        int checked = 0;
        for (int percentage = -10; percentage <= 150; percentage++) {
            String[] actual = renderPercentageArguments(percentage);
            boolean supported = percentage >= 10 && percentage <= 100;
            if (supported) {
                if (actual.length != 2 || !actual[0].equals("ffrs")
                        || !actual[1].equals("-ExecCmds=r.ScreenPercentage " + percentage))
                    throw new AssertionError("render arguments " + percentage);
            } else if (actual.length != 0) {
                throw new AssertionError("default arguments changed " + percentage);
            }
            checked++;
        }
        System.out.println("render argument cases=" + checked);
    }
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "RenderArguments.java"
            path.write_text(harness)
            subprocess.run(["javac", "--release", "8", "-d", temporary, str(path)], check=True)
            result = subprocess.run(["java", "-cp", temporary, "RenderArguments"],
                                    check=True, capture_output=True, text=True)
            self.assertIn("render argument cases=161", result.stdout)


if __name__ == "__main__":
    unittest.main()
