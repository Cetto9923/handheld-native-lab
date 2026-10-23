#!/bin/sh
# Run from this worktree root. Uses installed partial official NDK only.
# No downloads, game entry, Query binding, host TP writes or device operations.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,json,platform,struct,subprocess,zlib
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Host GuestContext contract and offline Android cross-link')
p.add_argument('--out',type=Path,required=True)
p.add_argument('--toolchain',type=Path,required=True)
p.add_argument('--rust-root',type=Path,default=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin')
a=p.parse_args();out=a.out.resolve();tc=a.toolchain.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must stay in this checkout .local')
source=ROOT/'src/runtime/hle/guest_context.c';test=ROOT/'tests/native/guest_context_contract.c'
if not source.is_file():p.error('run from the repository root')
out.mkdir(parents=True,exist_ok=False)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[source,source.with_suffix('.h'),test,ROOT/'scripts/runtime/build_guest_context_contract.sh']
sysroot=tc/'sysroot';resource=tc/'lib/clang/18';api=sysroot/'usr/lib/aarch64-linux-android/28'
builtins=resource/'lib/linux/libclang_rt.builtins-aarch64-android.a'
verified=json.loads((tc.parent/'validated-members.json').read_text())
if verified['source']!='https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip':raise ValueError('unexpected provenance')
known={v['path']:v for v in verified['members']}
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
 'dirty':subprocess.check_output(['git','status','--short'],text=True),'host':platform.platform(),
 'compiler':subprocess.check_output(['clang','--version'],text=True),
 'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},'cases':{},'official_members':{},
 'official_source':verified['source'],'whole_archive_sha1_verified':verified['whole_archive_sha1_verified'],
 'android_executed':False,'game_entry_executed':False,'query_memory_info_bound':False,
 'quota_source':'explicit synthetic contract inputs, not a selected game budget',
 'stack_test_bytes':65536,'stack_source':'explicit test input, matching coordinator-verified NPDM main stack size'}
def save():(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
for v in [api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',builtins,
          sysroot/'usr/include/pthread.h',sysroot/'usr/include/sys/mman.h']:
 name=str(v.relative_to(tc));data=v.read_bytes();crc=f'{zlib.crc32(data):08x}'
 if len(data)!=known[name]['bytes'] or crc!=known[name]['crc32']:raise ValueError('official CRC/size mismatch: '+name)
 manifest['official_members'][name]={'bytes':len(data),'crc32':crc,'sha256':digest(v)}
save()
def run(name,cmd):
 save()
 r=subprocess.run(['python3',str(ROOT/'scripts/runtime/run_capture.py'),'--out',str(out/name),'--timeout','30','--',*map(str,cmd)],capture_output=True,text=True)
 print(r.stdout.strip());entry=json.loads((out/name/'run.json').read_text());manifest['cases'][name]=entry;save()
 if r.returncode or entry['timed_out']:
  print((out/name/'stderr.log').read_text());raise RuntimeError(name+' failed')
 return (out/name/'stdout.log').read_text()
strict=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror']
for name,flags in [('normal',[]),('recovery',['-DGUEST_CONTEXT_TESTING=1'])]:
 binary=out/('guest_context_'+name+'.host')
 run('host-build-'+name,['clang',*strict,*flags,source,test,'-lpthread','-o',binary])
 log=run('host-run-'+name,[binary]);case=json.loads(log.splitlines()[-1])
 if case['errors'] or case['charged_after_join'] or case['pending_after_join'] or case['memory_info_valid_fields']:raise RuntimeError('contract accounting mismatch')
 manifest.setdefault('host_contracts',{})[name]=case
common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-resource-dir',resource,
 '-isystem',sysroot/'usr/include/aarch64-linux-android','-fPIC',*strict]
objects=[]
for name,path in [('guest_context',source),('contract',test)]:
 obj=out/(name+'.android.o');objects.append(obj)
 run('android-compile-'+name,['clang',*common,'-c',path,'-o',obj])
ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld';binary=out/'guest_context_contract.android'
run('android-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'-pie','--dynamic-linker','/system/bin/linker64',
 '--no-undefined','--eh-frame-hdr','-z','max-page-size=16384','-z','relro','-z','now','-z','noexecstack','--build-id=sha1',
 '-Map',out/'android.map','-o',binary,api/'crtbegin_dynamic.o',*objects,builtins,'-L',api,'-lc',api/'crtend_android.o'])
elf=run('android-inspect',[tc/'bin/llvm-readelf','--wide','--file-header','--program-headers','--dynamic','--symbols',binary])
data=binary.read_bytes()
if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise RuntimeError('not AArch64 ELF64')
if 'guest_context_test_' in elf:raise RuntimeError('failure hooks in production Android artifact')
manifest['android_artifact']={'path':str(binary),'bytes':len(data),'sha256':digest(binary),'test_hooks':False}
for name,value in manifest['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed while building: '+name)
for name,value in manifest['official_members'].items():
 if digest(tc/name)!=value['sha256']:raise RuntimeError('official input changed: '+name)
save();print('GuestContext host contracts passed; Android contract cross-linked, execution NOT_RUN')
PY
