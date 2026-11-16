#!/usr/bin/env python3
"""Build a local hactool with explicit inputs and no global title-key discovery."""
import argparse
import datetime
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

REVISION = '1d64a83450e025622f3468c28fc4164dad2c5ef6'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    url = f'https://codeload.github.com/SciresM/hactool/tar.gz/{REVISION}'
    plan = dict(revision=REVISION, url=url, license='ISC', cwd=str(output),
                started=datetime.datetime.now().astimezone().isoformat(), status='FETCHING')
    record = output / 'plan.json'
    record.write_text(json.dumps(plan, indent=2))
    archive = urllib.request.urlopen(url, timeout=45).read()
    (output / 'upstream.tar.gz').write_bytes(archive)
    plan['archive_sha256'] = hashlib.sha256(archive).hexdigest()
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as tar:
        for member in tar:
            path = Path(member.name)
            if path.is_absolute() or '..' in path.parts or member.issym() or member.islnk():
                raise ValueError('Unsupported archive path or link')
        tar.extractall(output)
    source = output / ('hactool-' + REVISION)
    main_c = source / 'main.c'
    text = main_c.read_text()
    replacements = {
        'FILE *homekeyfile = open_key_file((tool_ctx.action & ACTION_DEV) ? "dev" : "prod");':
            'FILE *homekeyfile = NULL; /* Caller supplies -k explicitly. */',
        'FILE *titlekeyfile = open_key_file("title");':
            'const char *titlekeys_path = getenv("FFRS_HACTOOL_TITLEKEYS");\n'
            '    FILE *titlekeyfile = titlekeys_path ? fopen(titlekeys_path, "rb") : NULL;',
        'base_ctx.settings.known_titlekeys = tool_ctx.settings.known_titlekeys;':
            'base_ctx.settings.known_titlekeys = tool_ctx.settings.known_titlekeys;\n'
            '                base_ctx.settings.suppress_keydata_output = tool_ctx.settings.suppress_keydata_output;',
    }
    for old, new in replacements.items():
        if text.count(old) != 1:
            raise ValueError('Upstream source does not match scoped-input patch')
        text = text.replace(old, new)
    main_c.write_text(text)
    (source / 'config.mk').write_text((source / 'config.mk.template').read_text())
    command = ['make', '-j4']
    plan.update(status='BUILDING', command=command, build_cwd=str(source),
                log=str(output / 'build.log'), patched_main_sha256=hashlib.sha256(text.encode()).hexdigest())
    record.write_text(json.dumps(plan, indent=2))
    with (output / 'build.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=source, stdout=log, stderr=subprocess.STDOUT)
        plan['pid'] = process.pid
        record.write_text(json.dumps(plan, indent=2))
        code = process.wait()
    plan['exit'] = code
    if code == 0:
        binary = source / 'hactool'
        plan.update(status='BUILT', binary=str(binary),
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    else:
        plan['status'] = 'BUILD_FAILED'
    plan['finished'] = datetime.datetime.now().astimezone().isoformat()
    record.write_text(json.dumps(plan, indent=2))
    if code:
        raise SystemExit(code)
    print(plan['binary'], flush=True)


if __name__ == '__main__':
    main()
