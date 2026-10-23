#!/bin/sh
# Run from this checkout. Host first; optional cross-link uses existing inputs.
# No device/game execution, TP writes, SDK Result translation or downloads.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,json,platform,struct,subprocess,zlib
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Real guest page ownership normal/recovery contracts')
p.add_argument('--out',type=Path,required=True)
p.add_argument('--toolchain',type=Path)
p.add_argument('--rust-root',type=Path,default=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin')
a=p.parse_args();out=a.out.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must remain in checkout .local')
names=['src/runtime/hle/guest_context.c','src/runtime/hle/address_region.c',
       'src/runtime/hle/guest_pages.c','tests/native/guest_pages_contract.c']
sources=[ROOT/v for v in names]
inputs=[*sources,*[ROOT/('src/runtime/hle/'+v+'.h') for v in ['guest_context','address_region','guest_pages']],
        ROOT/'scripts/runtime/build_guest_pages_contract.sh',ROOT/'scripts/runtime/run_capture.py']
if not all(v.is_file() for v in inputs):p.error('run from repository root with existing dependencies')
out.mkdir(parents=True,exist_ok=False)
def digest(v):return hashlib.sha256(v.read_bytes()).hexdigest()
m={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
   'dirty':subprocess.check_output(['git','status','--short'],text=True),
   'host':platform.platform(),'compiler':subprocess.check_output(['clang','--version'],text=True),
   'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},
   'cases':{},'host_contracts':{},'complete':False,'android_executed':False,
   'game_executed':False,'device_operations':False,'production_test_hooks':False,
   'quota_bytes':131072,'page_request_bytes':65536,
   'quota_source':'explicit self-owned contract input, not physical RAM or an SDK policy',
   'recovery_instrumentation':'existing raw VM hooks and test-only link wrappers for Context reserve/commit; no production additions',
   'limits':['one exclusive active owner of global raw ledger','exact allocation free only',
             'external quiescence for free/teardown','existing whole munmap failure aborts',
             'no SDK Result mapping, import binding or game execution']}
m['public_interface_sha256']=m['source_sha256']['src/runtime/hle/guest_pages.h']
m['source_bundle_sha256']=hashlib.sha256(json.dumps(m['source_sha256'],sort_keys=True,separators=(',',':')).encode()).hexdigest()
def save():(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
def run(name,cmd):
 save()
 r=subprocess.run(['python3',str(ROOT/'scripts/runtime/run_capture.py'),'--out',str(out/name),
     '--timeout','30','--',*map(str,cmd)],capture_output=True,text=True)
 print(r.stdout.strip())
 entry=json.loads((out/name/'run.json').read_text());m['cases'][name]=entry;save()
 if r.returncode or entry['timed_out']:
  print((out/name/'stderr.log').read_text());raise RuntimeError(name+' failed')
 return (out/name/'stdout.log').read_text()
strict=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror']
for mode in ['normal','recovery']:
 objects=[]
 for path in sources:
  flags=[]
  if mode=='recovery':
   if path.name=='guest_context.c':flags=['-Dguest_charge_reserve=guest_charge_reserve_real','-Dguest_charge_commit=guest_charge_commit_real']
   elif path.name=='address_region.c':flags=['-DNSO_OWNED_PAGES_TESTING=1']
   elif path.name=='guest_pages_contract.c':flags=['-DNSO_OWNED_PAGES_TESTING=1','-DGUEST_PAGES_CONTRACT_WRAP_CONTEXT=1']
  obj=out/(mode+'-'+path.stem+'.o');objects.append(obj)
  run('host-compile-'+mode+'-'+path.stem,['clang',*strict,*flags,'-c',path,'-o',obj])
 binary=out/('guest_pages_'+mode+'.host')
 run('host-link-'+mode,['clang',*objects,'-lpthread','-o',binary])
 text=run('host-run-'+mode,[binary]);case=json.loads(text.splitlines()[-1])
 if case['errors'] or any(case[v] for v in ['final_pending','final_charged','final_tickets','raw_regions']):raise RuntimeError('nonempty final ownership')
 if case['recovery_cases'] != (6 if mode=='recovery' else 0):raise RuntimeError('recovery coverage mismatch')
 m['host_contracts'][mode]=case
 m.setdefault('host_artifacts',{})[mode]={'path':str(binary),'sha256':digest(binary),'bytes':binary.stat().st_size}
symbols=run('host-production-symbols',['nm','-g',out/'guest_pages_normal.host'])
if any(v in symbols for v in ['nso_host_owned_pages_test_vm','guest_charge_reserve_real','guest_charge_commit_real','guest_context_test_']):raise RuntimeError('test symbols in production host binary')
if a.toolchain:
 tc=a.toolchain.resolve();sysroot=tc/'sysroot';resource=tc/'lib/clang/18'
 api=sysroot/'usr/lib/aarch64-linux-android/28'
 builtins=resource/'lib/linux/libclang_rt.builtins-aarch64-android.a'
 provenance=tc.parent/'validated-members.json';verified=json.loads(provenance.read_text())
 if verified['source']!='https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip':raise ValueError('unexpected official provenance')
 known={v['path']:v for v in verified['members']}
 m['official_source']=verified['source'];m['whole_archive_sha1_verified']=verified['whole_archive_sha1_verified']
 m['provenance_sha256']=digest(provenance);m['toolchain']=str(tc);m['official_members']={}
 for v in [api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',builtins,
           sysroot/'usr/include/pthread.h',sysroot/'usr/include/sys/mman.h']:
  name=str(v.relative_to(tc));data=v.read_bytes();crc=f'{zlib.crc32(data):08x}'
  if len(data)!=known[name]['bytes'] or crc!=known[name]['crc32']:raise ValueError('CRC/size mismatch: '+name)
  m['official_members'][name]={'bytes':len(data),'crc32':crc,'sha256':digest(v)}
 common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-resource-dir',resource,
         '-isystem',sysroot/'usr/include/aarch64-linux-android','-fPIC',*strict]
 objects=[]
 for path in sources:
  obj=out/(path.stem+'.android.o');objects.append(obj)
  run('android-compile-'+path.stem,['clang',*common,'-c',path,'-o',obj])
 ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld';binary=out/'guest_pages_contract.android'
 run('android-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'-pie','--dynamic-linker','/system/bin/linker64',
     '--no-undefined','--eh-frame-hdr','-z','max-page-size=16384','-z','relro','-z','now','-z','noexecstack','--build-id=sha1',
     '-Map',out/'android.map','-o',binary,api/'crtbegin_dynamic.o',*objects,builtins,'-L',api,'-lc',api/'crtend_android.o'])
 elf=run('android-inspect',[tc/'bin/llvm-readelf','--wide','--file-header','--program-headers','--dynamic','--symbols',binary])
 data=binary.read_bytes()
 if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise RuntimeError('not AArch64 ELF64')
 if any(v in elf for v in ['nso_host_owned_pages_test_vm','guest_charge_reserve_real','guest_charge_commit_real','guest_context_test_']):raise RuntimeError('test symbols in Android binary')
 m['android_artifact']={'path':str(binary),'bytes':len(data),'sha256':digest(binary),'test_hooks':False}
 for name,value in m['official_members'].items():
  if digest(tc/name)!=value['sha256']:raise RuntimeError('official input changed: '+name)
 if digest(provenance)!=m['provenance_sha256']:raise RuntimeError('provenance changed')
for name,value in m['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed during build: '+name)
m['complete']=True;save()
print('Guest pages host normal/recovery passed; optional Android execution NOT_RUN')
PY
