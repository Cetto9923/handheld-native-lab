#!/usr/bin/env python3
"""Reproduce the six-load Honor dark VS counterfactual; never install it.

Requires the actual captured VS867, not an arbitrary shader with the same key.
This is diagnostic evidence, not a general Maxwell translation repair. The
half-buffer branch and other shader variants remain outside its tested scope.
Generated game shaders belong only in an explicitly selected .local directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ACTUAL_SHA = '161df822093faebfafea89b18699514530c40dbc5dc179dc8b9d945d9e0915a9'
TESTED_SHA = 'f95a83a7ddb422c3c632d900f72cd7bdeb5c56b70c77777d68686a571626ab29'
GROUPS = (
    ('sc', ('%2770', '%2774', '%2698'), '%2716', '%2712'),
    ('pc', ('%2202', '%2206', '%2110'), '%2126', '%2122'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def restore_reads(assembly):
    changes = []
    for prefix, pointers, base, stride in GROUPS:
        for component, pointer in enumerate(pointers):
            needle = '               OpStore ' + pointer + ' %float_0'
            if assembly.count(needle) != 1:
                raise ValueError('expected one exact dropped-read store: ' + pointer)
            tag = '%' + prefix
            k = str(component)
            code = [
                f'{tag}_base{k} = OpLoad %int {base}',
                f'{tag}_stride{k} = OpLoad %int {stride}',
                f'{tag}_sampler{k} = OpLoad %14442 %vp_t_cb196610_1057005A',
                f'{tag}_image{k} = OpImage %14441 {tag}_sampler{k}',
            ]
            coordinate = f'{tag}_base{k}'
            if component:
                code.extend([
                    f'{tag}_step{k} = OpIMul %int {tag}_stride{k} %int_{k}',
                    f'{tag}_coord{k} = OpIAdd %int {tag}_base{k} {tag}_step{k}',
                ])
                coordinate = f'{tag}_coord{k}'
            code.extend([
                f'{tag}_fetch{k} = OpImageFetch %v4uint {tag}_image{k} {coordinate} None',
                f'{tag}_bits{k} = OpCompositeExtract %uint {tag}_fetch{k} 0',
                f'{tag}_float{k} = OpBitcast %float {tag}_bits{k}',
                f'OpStore {pointer} {tag}_float{k}',
            ])
            assembly = assembly.replace(needle, '\n'.join(code))
            changes.append(dict(pointer=pointer, binding=24, component=component,
                                base_pointer=base, stride_pointer=stride))
    return assembly, changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shader', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    source = args.shader.read_bytes()
    if digest(source) != ACTUAL_SHA:
        parser.error('input is not the verified captured VS867; no output written')
    root = Path(__file__).resolve().parents[2]
    destination = args.out.resolve()
    if not destination.is_relative_to(root / '.local'):
        parser.error('generated game shader output must be under this worktree/.local')
    if destination.exists():
        parser.error('output exists; preserve the prior experiment')
    with tempfile.TemporaryDirectory(prefix='honor-dark-dis-') as work:
        original = Path(work) / 'actual.spvasm'
        subprocess.run(['spirv-dis', str(args.shader), '-o', str(original)], check=True)
        patched, changes = restore_reads(original.read_text())
    destination.mkdir(parents=True)
    assembly = destination / 'six-load-vs.spvasm'
    output = destination / 'sh867'
    assembly.write_text(patched)
    subprocess.run(['spirv-as', '--target-env', 'vulkan1.1', str(assembly), '-o', str(output)], check=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.1', str(output)], check=True)
    result = digest(output.read_bytes())
    manifest = dict(input=str(args.shader.resolve()), input_sha256=ACTUAL_SHA,
                    output_sha256=result, byte_identical_to_GPU_tested_shader=result == TESTED_SHA,
                    changes=changes, scope='diagnostic float-buffer path only; no install or production PASS')
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if result != TESTED_SHA:
        raise RuntimeError('generated shader differs from GPU-tested artifact; do not use as equivalent')
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
