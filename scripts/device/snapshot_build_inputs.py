#!/usr/bin/env python3
"""Fingerprint source, build configuration and checkers without loading inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--runtime', type=Path, required=True)
parser.add_argument('--additional-dir', type=Path, action='append', default=[])
args = parser.parse_args()
root = args.source_root.resolve()
paths = set()
for directory in (root / 'src/android', root / 'scripts/device', root / 'tests/device',
                  args.runtime, *args.additional_dir):
    if directory.exists():
        paths.update(p for p in directory.rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.name != '.DS_Store' and p.suffix != '.pyc')

def git(directory, *arguments):
    return subprocess.run(['git', '-C', str(directory), *arguments], capture_output=True,
                          text=True, check=True).stdout.strip()

commit = os.environ.get('FFRS_DEPENDENCY_REVISION') or git(args.runtime, 'rev-parse', 'HEAD')
print(json.dumps({'source_root': str(root), 'source_HEAD': git(root, 'rev-parse', 'HEAD'),
    'source_dirty': git(root, 'status', '--porcelain'), 'runtime_source_revision': commit,
    'files': [{'path': str(p.resolve()), 'bytes': p.stat().st_size,
               'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(paths)]}, indent=2))
