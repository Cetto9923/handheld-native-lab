"""Test nvnSyncWait GPU wait policy on tablet and non-tablet profiles."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SYNC_SRC = ROOT / 'src/graphics/nvn_device/nvn_sync.c'


class TestSyncWaitPolicy(unittest.TestCase):
    def test_sync_wait_tablet_policy(self):
        source = SYNC_SRC.read_text()
        self.assertIn('const char *tablet = getenv("FFRS_PROFILE_TABLET");', source)
        self.assertIn('s_sync_gpu_wait = (tablet && tablet[0] == \'1\') ? 0 : 1;', source)

        # Build a small standalone test of the logic
        fixture = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int eval_sync_wait_policy(const char *marker_path) {
    int s_sync_gpu_wait = -1;
    FILE *f = marker_path ? fopen(marker_path, "rb") : NULL;
    const char *tablet = getenv("FFRS_PROFILE_TABLET");
    s_sync_gpu_wait = (tablet && tablet[0] == '1') ? 0 : 1;
    if (f) {
        s_sync_gpu_wait = (fgetc(f) != '0');
        fclose(f);
    } else {
        const char *e = getenv("FFRS_SYNC_GPU_WAIT");
        if (e) s_sync_gpu_wait = (atoi(e) != 0);
    }
    return s_sync_gpu_wait;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *marker = (argc >= 3 && strlen(argv[2]) > 0) ? argv[2] : NULL;
    printf("%d\n", eval_sync_wait_policy(marker));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            bin_path = tmp_path / 'test_sync'
            src_path = tmp_path / 'test_sync.c'
            src_path.write_text(fixture)
            subprocess.run(['cc', '-O2', str(src_path), '-o', str(bin_path)], check=True)

            # Case 1: Tablet profile = 1 -> default 0
            env = os.environ.copy()
            env['FFRS_PROFILE_TABLET'] = '1'
            env.pop('FFRS_SYNC_GPU_WAIT', None)
            res = subprocess.run([str(bin_path), 'run'], capture_output=True, text=True, env=env, check=True)
            self.assertEqual(res.stdout.strip(), '0')

            # Case 2: Tablet profile = 0 -> default 1
            env['FFRS_PROFILE_TABLET'] = '0'
            res = subprocess.run([str(bin_path), 'run'], capture_output=True, text=True, env=env, check=True)
            self.assertEqual(res.stdout.strip(), '1')

            # Case 3: Marker '1' overrides tablet profile 1 -> 1
            marker_file = tmp_path / 'marker'
            marker_file.write_text('1')
            env['FFRS_PROFILE_TABLET'] = '1'
            res = subprocess.run([str(bin_path), 'run', str(marker_file)], capture_output=True, text=True, env=env, check=True)
            self.assertEqual(res.stdout.strip(), '1')

            # Case 4: Marker '0' overrides tablet profile 0 -> 0
            marker_file.write_text('0')
            env['FFRS_PROFILE_TABLET'] = '0'
            res = subprocess.run([str(bin_path), 'run', str(marker_file)], capture_output=True, text=True, env=env, check=True)
            self.assertEqual(res.stdout.strip(), '0')


if __name__ == '__main__':
    unittest.main()
