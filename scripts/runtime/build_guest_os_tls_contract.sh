#!/bin/sh
# Self-owned OS-TLS contracts; optional Android cross-link is offline only.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,json,platform,struct,subprocess,zlib
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Independent OS TLS slot/lifecycle contracts')
p.add_argument('--out',type=Path,required=True)
p.add_argument('--toolchain',type=Path)
p.add_argument('--rust-root',type=Path,default=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin')
a=p.parse_args();out=a.out.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must remain in this checkout .local')
source=ROOT/'src/runtime/hle/guest_os_tls.c';header=source.with_suffix('.h')
test=ROOT/'tests/native/guest_os_tls_contract.c'
reports=[ROOT/'docs/analysis'/v for v in ['codex_tls_sdk_lifecycle_report.md','codex_tls_exit_hook_report.md','codex_runtime_report.md']]
inputs=[source,header,test,ROOT/'scripts/runtime/build_guest_os_tls_contract.sh',ROOT/'scripts/runtime/run_capture.py',ROOT/'AGENTS.md',*reports]
if not all(v.is_file() for v in inputs):p.error('run from the checkout root with immutable reports')
out.mkdir(parents=True,exist_ok=False)
def digest(v):return hashlib.sha256(v.read_bytes()).hexdigest()
m={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
   'dirty':subprocess.check_output(['git','status','--short'],text=True),
   'host':platform.platform(),'compiler':subprocess.check_output(['clang','--version'],text=True),
   'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},'cases':{},'host_contracts':{},
   'complete':False,'android_executed':False,'device_operations':False,'game_executed':False,
   'original_module_bytes_read':False,'host_tp_modified':False,'hle_bound':False,
   'production_test_hooks':False,'source_reports':[str(v.relative_to(ROOT)) for v in reports],
   'contract':{'slots':32,'app_order':'0..15 ascending','sdk_order':'31..17 descending','reserved':16,
       'max_cleanup_scans':5,'value_bits':64,'output_slot_bits':32,'native_workers':8,'cycles_per_worker':16},
   'limits':['opaque registered threads, no native identity/SDK ThreadType/TP layout',
       'guest destructor identity invoked only by explicit typed provider',
       'one active cleanup per manager, quiescent external value users',
       'provider failure quarantines manager/thread and permanently retains their state',
       'five scans do not guarantee all values zero; no guest object freed by component',
       'no ELF TLS/destructor list, SDK Result mapping, host TP or import binding']}
m['public_interface_sha256']=digest(header)
m['source_bundle_sha256']=hashlib.sha256(json.dumps(m['source_sha256'],sort_keys=True,separators=(',',':')).encode()).hexdigest()
def save():(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
def run(name,cmd):
 save()
 r=subprocess.run(['python3',str(ROOT/'scripts/runtime/run_capture.py'),'--out',str(out/name),
     '--timeout','30','--',*map(str,cmd)],capture_output=True,text=True)
 print(r.stdout.strip());entry=json.loads((out/name/'run.json').read_text());m['cases'][name]=entry;save()
 if r.returncode or entry['timed_out']:
  print((out/name/'stderr.log').read_text());raise RuntimeError(name+' failed')
 return (out/name/'stdout.log').read_text()
strict=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror']
owner=out/'guest_os_tls.host.o'
run('host-compile-owner',['clang',*strict,'-c',source,'-o',owner])
m['production_host_object']={'sha256':digest(owner),'bytes':owner.stat().st_size,'shared_by_normal_and_recovery':True}
for mode in ['normal','recovery']:
 obj=out/(mode+'-contract.host.o');binary=out/('guest_os_tls_'+mode+'.host')
 flags=['-DGUEST_OS_TLS_CONTRACT_RECOVERY=1'] if mode=='recovery' else []
 run('host-compile-'+mode,['clang',*strict,*flags,'-c',test,'-o',obj])
 run('host-link-'+mode,['clang',owner,obj,'-lpthread','-o',binary])
 log=run('host-run-'+mode,[binary]);case=json.loads(log.splitlines()[-1])
 if case['errors'] or case['reentry_cases']!=1:raise RuntimeError('contract failed')
 retained=2 if mode=='recovery' else 0
 if any(case[v]!=retained for v in ['recovery_cases','retained_managers','retained_threads']):raise RuntimeError('quarantine ownership mismatch')
 m['host_contracts'][mode]=case
 m.setdefault('host_artifacts',{})[mode]={'path':str(binary),'sha256':digest(binary),'bytes':binary.stat().st_size}
symbols=run('host-production-symbols',['nm','-g',out/'guest_os_tls_normal.host'])
if 'guest_os_tls_test_' in symbols:raise RuntimeError('production test hook')
if a.toolchain:
 tc=a.toolchain.resolve();sysroot=tc/'sysroot';resource=tc/'lib/clang/18';api=sysroot/'usr/lib/aarch64-linux-android/28'
 builtins=resource/'lib/linux/libclang_rt.builtins-aarch64-android.a';provenance=tc.parent/'validated-members.json'
 verified=json.loads(provenance.read_text());known={v['path']:v for v in verified['members']}
 if verified['source']!='https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip':raise ValueError('unexpected provenance')
 m['official_source']=verified['source'];m['whole_archive_sha1_verified']=verified['whole_archive_sha1_verified']
 m['provenance_sha256']=digest(provenance);m['toolchain']=str(tc);m['official_members']={}
 for v in [api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',builtins,sysroot/'usr/include/pthread.h']:
  name=str(v.relative_to(tc));data=v.read_bytes();crc=f'{zlib.crc32(data):08x}'
  if len(data)!=known[name]['bytes'] or crc!=known[name]['crc32']:raise ValueError('CRC/size mismatch: '+name)
  m['official_members'][name]={'bytes':len(data),'crc32':crc,'sha256':digest(v)}
 common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-resource-dir',resource,
         '-isystem',sysroot/'usr/include/aarch64-linux-android','-fPIC',*strict]
 objects=[]
 for path in [source,test]:
  obj=out/(path.stem+'.android.o');objects.append(obj)
  run('android-compile-'+path.stem,['clang',*common,'-c',path,'-o',obj])
 ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld';binary=out/'guest_os_tls_contract.android'
 run('android-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'-pie','--dynamic-linker','/system/bin/linker64',
     '--no-undefined','--eh-frame-hdr','-z','max-page-size=16384','-z','relro','-z','now','-z','noexecstack','--build-id=sha1',
     '-Map',out/'android.map','-o',binary,api/'crtbegin_dynamic.o',*objects,builtins,'-L',api,'-lc',api/'crtend_android.o'])
 elf=run('android-inspect',[tc/'bin/llvm-readelf','--wide','--file-header','--program-headers','--dynamic','--symbols',binary])
 data=binary.read_bytes()
 if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise RuntimeError('not AArch64 ELF64')
 if 'guest_os_tls_test_' in elf:raise RuntimeError('production Android test hook')
 m['android_artifact']={'path':str(binary),'bytes':len(data),'sha256':digest(binary),'test_hooks':False}
 for name,value in m['official_members'].items():
  if digest(tc/name)!=value['sha256']:raise RuntimeError('official input changed: '+name)
 if digest(provenance)!=m['provenance_sha256']:raise RuntimeError('provenance changed')
for name,value in m['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed during build: '+name)
m['complete']=True;save()
print('OS TLS host contracts passed; recovery retains two quarantined managers/threads; Android execution NOT_RUN')
PY
