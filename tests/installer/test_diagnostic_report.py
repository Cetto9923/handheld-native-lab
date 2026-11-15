"""Compile the real Java ZIP writer and exercise self-authored diagnostic fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / "src/android/app/java/org/ffrs/diagnostic"


class DiagnosticReport(unittest.TestCase):
    def test_actual_writer_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            classes = Path(temp) / "classes"
            classes.mkdir()
            subprocess.run(["javac", "--release", "8", "-d", str(classes),
                            str(JAVA / "DiagnosticReport.java"),
                            str(ROOT / "tests/installer/DiagnosticReportContract.java")], check=True)
            result = subprocess.run(["java", "-cp", str(classes),
                                     "org.ffrs.diagnostic.DiagnosticReportContract",
                                     str(Path(temp) / "fixtures")],
                                    check=True, capture_output=True, text=True)
            self.assertIn("DiagnosticReportContract PASS 6", result.stdout)

    def test_standalone_activity_compiles_against_android_34(self):
        sdk = Path(os.environ.get("ANDROID_HOME", Path.home() / "Library/Android/sdk"))
        jar = Path(os.environ.get("FFRS_ANDROID_JAR", sdk / "platforms/android-34/android.jar"))
        if not jar.is_file():
            self.skipTest(f"Android 34 jar unavailable at {jar}")
        with tempfile.TemporaryDirectory() as temp:
            subprocess.run(["javac", "--release", "8", "-classpath", str(jar),
                            "-d", temp, str(JAVA / "DiagnosticReport.java"),
                            str(JAVA / "ReportActivity.java"),
                            str(JAVA / "DeviceProfile.java")], check=True)
            self.assertFalse(list(Path(temp).rglob("GameWindowNative*.class")))

    def test_launcher_shortcut_targets_standalone_activity(self):
        android = "{http://schemas.android.com/apk/res/android}"
        root = ET.parse(ROOT / "src/android/app/gamewindow/res/xml/report_shortcuts.xml").getroot()
        shortcut = root.find("shortcut")
        self.assertEqual(shortcut.get(android + "shortcutShortLabel"), "@string/report_shortcut_label")
        intent = shortcut.find("intent")
        self.assertEqual(intent.get(android + "targetPackage"), "org.ffrs.gamewindow")
        self.assertEqual(intent.get(android + "targetClass"), "org.ffrs.diagnostic.ReportActivity")

    def test_manifest_build_and_menu_wiring(self):
        android = "{http://schemas.android.com/apk/res/android}"
        root = ET.parse(ROOT / 'src/android/app/gamewindow/AndroidManifest.xml').getroot()
        activities = {a.get(android + 'name'): a for a in root.findall('./application/activity')}
        report = activities['org.ffrs.diagnostic.ReportActivity']
        self.assertEqual(report.get(android + 'process'), ':diagnostics')
        self.assertEqual(report.get(android + 'exported'), 'true')
        game = activities['org.ffrs.diagnostic.GameActivity']
        shortcut = game.find("meta-data[@" + android + "name='android.app.shortcuts']")
        self.assertEqual(shortcut.get(android + 'resource'), '@xml/report_shortcuts')
        build = (ROOT / 'scripts/android/build_game_window_apk.sh').read_text()
        self.assertIn('${FFRS_APP_JAVA}/DiagnosticReport.java', build)
        self.assertIn('${FFRS_APP_JAVA}/ReportActivity.java', build)
        source = (JAVA / 'GameActivity.java').read_text()
        self.assertIn('new Intent(owner, ReportActivity.class)', source)
        labels = ET.parse(ROOT / 'src/android/app/gamewindow/res/values/strings.xml').getroot()
        self.assertEqual(labels.find("string[@name='report_shortcut_label']").text, '导出诊断报告')


if __name__ == "__main__":
    unittest.main()
