#!/usr/bin/env python3
"""Execute the real display JNI bridge on a host JVM with synthetic inputs only."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--classes', type=Path, required=True)
parser.add_argument('--library-dir', type=Path, required=True)
args = parser.parse_args()
result = subprocess.run(['java', '-Xcheck:jni', f'-Djava.library.path={args.library_dir.resolve()}',
                         '-cp', str(args.classes), 'org.ffrs.diagnostic.DisplaySnapshotJniTest'],
                        text=True, capture_output=True, check=True)
decoder = json.JSONDecoder()
text = result.stdout
cases = {}
while text.strip():
    record, end = decoder.raw_decode(text.lstrip())
    cases[record['case']] = record['report']['display']
    text = text.lstrip()[end:]
assert len(cases) == 11
p = cases['portrait']
assert p['status'] == 'ok' and (p['width_px'], p['height_px']) == (1080, 2340)
assert p['content_height_px'] == 2184 and p['density_dpi'] == 440 and p['density'] == 2.75
assert p['refresh_rate_hz'] == 59.94 and p['modes'][1]['refresh_rate_hz'] == 120
assert p['refresh_rate_scope'] == 'display_refresh_not_game_fps'
assert p['system_bar_cutout_insets_px'] == {'status': 'measured_android_api', 'left': 0, 'top': 66, 'right': 0, 'bottom': 90}
assert p['captured_elapsed_realtime_ns'] == 123456789 and p['current_mode_id'] == 1
l = cases['landscape']
assert (l['width_px'], l['height_px'], l['rotation']) == (2340, 1080, 1)
assert (l['modes'][0]['width_px'], l['modes'][0]['height_px']) == (1080, 2340)
assert cases['tablet_synthetic']['width_px'] == 3000 and cases['tablet_synthetic']['refresh_rate_hz'] == 144
m = cases['multiwindow']
assert (m['width_px'], m['height_px']) == (1500, 900) and m['modes'][0]['width_px'] == 3000
assert cases['unknown_refresh']['refresh_rate_hz'] == 0 and cases['unknown_refresh']['status'].startswith('partial')
assert cases['unknown_refresh']['refresh_rate_status'] == 'unavailable'
assert cases['unknown_density']['density'] == cases['unknown_density']['density_dpi'] == 0
for name in ('missing_size', 'bad_mode_arrays', 'no_snapshot', 'wrong_object'):
    d = cases[name]
    assert d['status'].startswith('unavailable') and d['width_px'] == d['height_px'] == d['refresh_rate_hz'] == 0
    assert not d['modes']
assert cases['no_modes']['modes'] == [] and cases['no_modes']['modes_status'] == 'unavailable'
assert not result.stderr, result.stderr
print('HOST_JVM_SYNTHETIC_INTERFACE_ONLY: 11 Java -> production JNI -> collector -> JSON cases passed with -Xcheck:jni. Android display/device/audio/game execution NOT_RUN.')
