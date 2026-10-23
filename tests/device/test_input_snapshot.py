#!/usr/bin/env python3
"""Production JNI/collector/JSON exercised by a real JVM with synthetic capabilities."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--classes', type=Path, required=True)
parser.add_argument('--library-dir', type=Path, required=True)
args = parser.parse_args()
result = subprocess.run(['java', '-Xcheck:jni', f'-Djava.library.path={args.library_dir.resolve()}',
                         '-cp', str(args.classes), 'org.ffrs.diagnostic.InputSnapshotJniTest'],
                        text=True, capture_output=True, check=True)
decoder = json.JSONDecoder()
text = result.stdout
cases = {}
while text.strip():
    record, end = decoder.raw_decode(text.lstrip())
    cases[record['case']] = record['report']['input']
    text = text.lstrip()[end:]
assert len(cases) == 18
j = cases['joystick']
assert j['status'] == 'ok' and j['gamepad_count'] == 1 and j['enumeration_complete']
assert j['gamepad_count_status'] == 'measured_android_api'
assert j['gamepad_count_unit'] == 'android_input_devices_supporting_gamepad_or_joystick'
assert j['source'] == 'InputDevice.getDeviceIds/getDevice/hasKeys/getMotionRanges'
assert j['captured_elapsed_realtime_ns'] == 123456789
assert j['key_code_namespace'] == 'android.view.KeyEvent.KEYCODE_*'
assert j['axis_code_namespace'] == 'android.view.MotionEvent.AXIS_*'
assert j['scope'] == 'reported_device_capabilities_not_event_or_game_mapping_validation'
d = j['devices'][0]
assert (d['id'], d['name'], d['vendor_id'], d['product_id'], d['controller_number']) == (5, 'Unbranded input', 0x1234, 0xabcd, 1)
assert d['source_mask'] == 0x01000010 and d['sources'] == ['SOURCE_JOYSTICK']
assert d['is_gamepad'] and not d['is_virtual'] and d['key_codes'] == []
assert d['axes'] == [{'type': 0, 'name': 'AXIS_X', 'source_mask': 0x01000010,
                      'min': -1, 'max': 1, 'flat': 0.0625, 'fuzz': 0.03125, 'resolution': 0.5}]
m = cases['multiple_sources']
assert m['gamepad_count'] == 2 and len(m['devices']) == 4
assert m['devices'][1]['sources'] == ['SOURCE_KEYBOARD', 'SOURCE_GAMEPAD']
assert m['devices'][1]['key_codes'] == [4, 96, 97, 108]  # Android BACK/A/B/START, not evdev BTN codes.
assert m['devices'][2]['id'] == -1 and m['devices'][2]['is_virtual']
assert not m['devices'][2]['is_gamepad']  # A gamepad-looking name is not evidence.
assert not m['devices'][3]['is_gamepad']  # Motion axes on a touchscreen do not imply a gamepad.
e = cases['measured_empty']
assert e['status'] == 'ok' and e['devices'] == [] and e['gamepad_count'] == 0
assert e['enumeration_complete'] and e['gamepad_count_status'] == 'measured_android_api'
p = cases['partial_capture']
assert p['status'] == p['gamepad_count_status'] == 'partial_android_api_measurement'
assert not p['enumeration_complete'] and p['gamepad_count'] == 1 and p['error']
assert not cases['source_class_bits_only']['devices'][0]['is_gamepad']
assert cases['source_class_bits_only']['gamepad_count'] == 0
for name in ('not_measured', 'missing_source', 'duplicate_id', 'nan_axis', 'reversed_axis', 'negative_key',
             'null_devices', 'null_device', 'null_axes', 'null_keys', 'oversize_devices', 'no_snapshot', 'wrong_object'):
    unknown = cases[name]
    assert unknown['status'].startswith('unavailable'), (name, unknown)
    assert not unknown['enumeration_complete'] and unknown['gamepad_count_status'] == 'unavailable'
    assert unknown['devices'] == [] and unknown['gamepad_count'] == 0
assert cases['no_snapshot']['status'] == 'unavailable_no_android_api_snapshot'
assert not result.stderr, result.stderr
print('HOST_JVM_SYNTHETIC_INTERFACE_ONLY: 18 Java -> production input JNI -> collector -> JSON cases passed with -Xcheck:jni. Android capability capture/event/game execution NOT_RUN.')
