#!/usr/bin/env python3
"""Check an APK does not embed the separately generated original leaf bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--apk', type=Path, required=True)
parser.add_argument('--reviewed-library', type=Path, required=True)
args = parser.parse_args()
raw = args.reviewed_library.read_bytes()
if hashlib.sha256(raw).hexdigest() != '8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8':
    raise SystemExit('Review library identity differs; no bytes were printed or copied')
# The immutable artifact's reviewed original function maps to file offset 0x28c.
leaf = raw[0x28c:0x28c + 44]
if hashlib.sha256(leaf).hexdigest() != '8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9':
    raise SystemExit('Original leaf identity differs')
with zipfile.ZipFile(args.apk) as apk:
    for member in apk.namelist():
        if member.endswith('libffrs_leaf.so') or leaf in apk.read(member):
            raise SystemExit(f'Original leaf bytes/library were embedded in APK member {member}')
print(json.dumps({'scope': 'STATIC_PACKAGING_ONLY', 'original_leaf_embedded': False,
                  'optional_library_packaged': False, 'android_leaf_execution': 'NOT_RUN'}))
