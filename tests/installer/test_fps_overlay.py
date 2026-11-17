"""Exercise the production frame counter, sampler, and packaged cheat-key path."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'src/android/app/java/org/ffrs/diagnostic'


class FpsOverlay(unittest.TestCase):
    def test_recreated_activity_keeps_the_running_guests_save_root(self):
        source = (JAVA / 'GameActivity.java').read_text()
        declaration = re.search(r'    private (?:static )?File liveSaveRoot;', source).group(0)
        start = source.index('    private File primarySaveSlot() {')
        method = source[start:source.index('\n    /**', start)]
        for restore_bug in (False, True):
            field = declaration.replace('static ', '') if restore_bug else declaration
            fixture = '''import java.io.File;
import java.nio.file.Files;
public class SaveRootContract {
''' + field + method + '''
    public static void main(String[] args) throws Exception {
        File root = new File(args[0]);
        File slot = new File(root, "GameSaveData0");
        Files.write(slot.toPath(), new byte[] {1, 2, 3});
        SaveRootContract first = new SaveRootContract();
        first.liveSaveRoot = root;
        SaveRootContract recreated = new SaveRootContract();
        if (!slot.equals(recreated.primarySaveSlot()))
            throw new AssertionError("running save root lost on Activity recreation");
    }
}
'''
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                java = path / 'SaveRootContract.java'
                java.write_text(fixture)
                subprocess.run(['javac', str(java)], check=True, capture_output=True)
                result = subprocess.run(['java', '-cp', directory, 'SaveRootContract', directory],
                                        capture_output=True, text=True)
                if restore_bug:
                    self.assertNotEqual(result.returncode, 0)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_surface_loss_preserves_guest_texture_acquisition(self):
        source = ROOT / 'src/graphics/nvn_device/nvn_window.c'
        for restore_bug in (False, True):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                implementation = source
                if restore_bug:
                    implementation = path / 'nvn_window.c'
                    original = source.read_text()
                    self.assertIn('if (!w || !count || count > w->num_textures ||', original)
                    implementation.write_text(original.replace(
                        'if (!w || !count || count > w->num_textures ||',
                        'if (!w || !w->swapchain || !count || count > w->num_textures ||', 1))
                command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                           '-fsanitize=address,undefined', '-pthread',
                           '-I' + str(source.parent), '-I' + str(ROOT / 'src/runtime/hle'),
                           str(implementation), str(source.with_name('nvn_sync.c')),
                           str(ROOT / 'src/runtime/hle/native_window.c'),
                           str(ROOT / 'tests/native/window_acquire_contract.c'),
                           '-o', str(path / 'window')]
                subprocess.run(command, check=True, capture_output=True, text=True)
                result = subprocess.run([str(path / 'window')], capture_output=True, text=True)
                if restore_bug:
                    self.assertNotEqual(result.returncode, 0)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_counter_counts_only_queued_game_frames(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_engine.c').read_text()
        start = source.index('static atomic_uint_fast64_t s_presented_frames;')
        end = source.index('/* The stderr', start)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            header = path / 'present.h'
            header.write_text(source[start:end])
            command = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-pthread',
                       '-DPRODUCTION_HEADER="' + str(header) + '"',
                       str(ROOT / 'tests/native/present_counter_contract.c'),
                       '-o', str(path / 'counter')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(path / 'counter')], check=True, capture_output=True)

    def test_sampler_and_direct_asset_key(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.index('    static final class FrameRateSampler {')
        end = source.index('    private static final class FpsUpdate', start)
        sampler = source[start:end].replace('static final class', 'final class', 1)
        test = '''package org.ffrs.diagnostic;
import java.io.ByteArrayInputStream;
import java.lang.reflect.Field;
import java.util.Arrays;
public class FpsContract {
    static void equal(double actual, double expected) {
        if (Math.abs(actual - expected) > 0.00001 || Double.isNaN(actual))
            throw new AssertionError("FPS " + actual + " expected " + expected);
    }
    public static void main(String[] args) throws Exception {
        FrameRateSampler rate = new FrameRateSampler();
        if (!Double.isNaN(rate.sample(10, 1000))) throw new AssertionError();
        equal(rate.sample(40, 2000), 30);
        equal(rate.sample(40, 4000), 0);
        equal(rate.sample(280, 6000), 120);
        if (!Double.isNaN(rate.sample(1, 7000))) throw new AssertionError();
        if (!Double.isNaN(rate.sample(1, 7000))) throw new AssertionError();
        rate.reset();
        if (!Double.isNaN(rate.sample(1, 8000))) throw new AssertionError();
        equal(rate.sample(31, 9000), 30);
        if (SaveCheater.initializeKey(new ByteArrayInputStream(new byte[31]))) throw new AssertionError();
        if (SaveCheater.initializeKey(new ByteArrayInputStream(new byte[33]))) throw new AssertionError();
        byte[] key = new byte[32];
        for (int i = 0; i < key.length; ++i) key[i] = (byte)i;
        if (!SaveCheater.initializeKey(new ByteArrayInputStream(key)) || !SaveCheater.available())
            throw new AssertionError("packaged key not ready");
        Field field = SaveCheater.class.getDeclaredField("cachedKey");
        field.setAccessible(true);
        if (!Arrays.equals(key, (byte[])field.get(null))) throw new AssertionError("binary key changed");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'Log.java').write_text('package android.util; public class Log { '
                                           'public static int w(String t,String m) { return 0; } }')
            (path / 'FrameRateSampler.java').write_text('package org.ffrs.diagnostic;\n' + sampler)
            (path / 'FpsContract.java').write_text(test)
            subprocess.run(['javac', '-d', str(path), str(path / 'Log.java'),
                            str(JAVA / 'SaveCheater.java'), str(path / 'FrameRateSampler.java'),
                            str(path / 'FpsContract.java')], check=True, capture_output=True, text=True)
            subprocess.run(['java', '-cp', str(path), 'org.ffrs.diagnostic.FpsContract'],
                           cwd=path, check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
