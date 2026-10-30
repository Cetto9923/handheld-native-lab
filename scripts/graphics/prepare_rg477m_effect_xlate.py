#!/usr/bin/env python3
"""Prepare the pinned upstream translator used by the RG477M shield candidate.

Outputs belong in .local. This prepares source/tools, never installs shaders.
Use build_ffrs_xgs.py with an existing game input and index for individual keys.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

REVISION = '8e0916bf04f6fb8542c52ed01b166a8796158ca0'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--dotnet', type=Path, help='local SDK executable; omit to prepare only')
    args = parser.parse_args()
    revision = subprocess.check_output(
        ['git', '-C', str(args.source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != REVISION:
        parser.error('expected reviewed upstream revision ' + REVISION)
    helper = Path(__file__).with_name('prepare_ffrs_xlate.py')
    spec = importlib.util.spec_from_file_location('ffrs_prepare', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # This upstream increased the pool bound. Keep that change; the existing
    # preparation inserts only the Vulkan nonconstant-offset fallback.
    module.ANCHOR = '        public int QueryTextureArrayLengthFromPool() => 1 << 20;\n'
    manifest = module.prepare(args.source, args.out)
    manifest['recipe'] = str(Path(__file__).resolve())
    manifest['recipe_scope'] = 'pinned upstream + existing Vulkan offset fallback; no guarded-Phi extension'
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return module.build(args.out, args.dotnet, manifest) if args.dotnet else 0


if __name__ == '__main__':
    raise SystemExit(main())
