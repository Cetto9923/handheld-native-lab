#!/bin/sh
# Offline, explicit-policy host/Android checkpoint. No device operations.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,importlib.util,json,platform,re,struct,subprocess,zlib
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Build the restricted owned startup experiment')
p.add_argument('--out',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True)
p.add_argument('--main',type=Path);p.add_argument('--quota-bytes',type=int);p.add_argument('--stack-bytes',type=int)
p.add_argument('--rust-root',type=Path,default=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin')
a=p.parse_args();out=a.out.resolve();tc=a.toolchain.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must stay in this checkout .local')
if a.main and (not a.quota_bytes or not a.stack_bytes):p.error('main requires explicit diagnostic quota/stack inputs')
out.mkdir(parents=True,exist_ok=False)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
nso=ROOT/'src/runtime/nso';hle=ROOT/'src/runtime/hle'
sources=[nso/'nso_loader.c',*[hle/(s+'.c') for s in ['guest_context','guest_process','nso_host','address_region','mutex','exit_registry']]]
test=ROOT/'tests/native/guest_process_contract.c';cli=nso/'nso_call.c'
inputs=[*sources,*[s.with_suffix('.h') for s in sources],test,cli,ROOT/'scripts/runtime/build_guest_process_contract.sh',ROOT/'tests/native/test_loader.py']
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
 'dirty':subprocess.check_output(['git','status','--short'],text=True),'host':platform.platform(),
 'compiler':subprocess.check_output(['clang','--version'],text=True),'cases':{},
 'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},'android_executed':False,
 'quota_policy':'required explicit native logical backing cap; not original SVC measurements',
 'heap_policy':'initialized immutable empty native manager; backing mutators unresolved',
 'lock_order':'process -> Context or process -> VA; external raw APIs quiescent',
 'game_started':False,'crt_tls_initialized':False}
def save():(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
def run(name,cmd,expected=0,timeout=30):
 save();r=subprocess.run(['python3',str(ROOT/'scripts/runtime/run_capture.py'),'--out',str(out/name),'--timeout',str(timeout),'--',*map(str,cmd)],capture_output=True,text=True)
 print(r.stdout.strip());entry=json.loads((out/name/'run.json').read_text());manifest['cases'][name]=entry;save()
 if r.returncode!=expected or entry['timed_out']:
  print((out/name/'stderr.log').read_text());raise RuntimeError(name+' failed')
 return (out/name/'stdout.log').read_text()
spec=importlib.util.spec_from_file_location('positive_loader_fixture',ROOT/'tests/native/test_loader.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture);fixture.fixture(out/'contract.nso')
manifest['fixture']={'path':str(out/'contract.nso'),'bytes':(out/'contract.nso').stat().st_size,'sha256':digest(out/'contract.nso')}
strict=['-std=c11','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror']
run('host-reject-untyped-guards',['clang',*strict,'-c',hle/'guest_process.c','-o',out/'unsupported-guards.o'],1)
if 'requires the real typed C++ guard runtime' not in (out/'host-reject-untyped-guards/stderr.log').read_text():raise RuntimeError('missing typed guard build constraint')
host=[]
for source in sources:
 obj=out/(source.stem+'.host.o');host.append(obj)
 run('host-compile-'+source.stem,['clang',*strict,'-DNSO_WITH_CXX_GUARDS=1','-c',source,'-o',obj])
run('host-build-normal',['clang',*strict,test,*host,'-lc++','-lpthread','-o',out/'guest_process_contract.host'])
log=run('host-contract-normal',[out/'guest_process_contract.host',out/'contract.nso'])
manifest['normal_contract']=json.loads(log.splitlines()[-1])
if manifest['normal_contract']['errors']:raise RuntimeError('normal contract failed')
run('host-build-loader-recovery',['clang',*strict,'-DNSO_LOADER_TESTING=1','-c',sources[0],'-o',out/'nso_loader.recovery.o'])
run('host-build-recovery',['clang',*strict,'-DNSO_LOADER_TESTING=1',test,out/'nso_loader.recovery.o',*host[1:],'-lc++','-lpthread','-o',out/'guest_process_recovery.host'])
log=run('host-contract-recovery',[out/'guest_process_recovery.host',out/'contract.nso'])
manifest['recovery_contract']=json.loads(log.splitlines()[-1])
if manifest['recovery_contract']['errors']:raise RuntimeError('recovery contract failed')
run('host-invalid-query',[out/'guest_process_contract.host','--invalid-query'],134)
if '[query-invalid]' not in (out/'host-invalid-query/stderr.log').read_text():raise RuntimeError('missing fatal invalid Query diagnostic')
run('host-build-cli',['clang',*strict,'-DNSO_WITH_HOST_HLE=1','-DNSO_WITH_GUEST_PROFILE=1',cli,*host,'-lc++','-lpthread','-o',out/'nso_owned_startup.host'])
legacy=[v for v in host if v.stem not in ['guest_context.host','guest_process.host']]
run('host-build-legacy-cli',['clang',*strict,'-DNSO_WITH_HOST_HLE=1',cli,*legacy,'-lc++','-lpthread','-o',out/'nso_legacy.host'])
run('host-legacy-info',[out/'nso_legacy.host',out/'contract.nso','--info'])
run('host-profile-info',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1048576','--guest-stack','65536','--info'])
run('host-profile-module-limit',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1','--guest-stack','65536','--info'],1)
if a.main:
 data=a.main.read_bytes();value=hashlib.sha256(data).hexdigest()
 if value!='6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5':raise RuntimeError('unreviewed main')
 file,mem,size=struct.unpack_from('<III',data,0x10);text=data[file:file+size]
 svc=[];msr=[]
 for match in re.finditer(b'[\xd4\xd5]',text):
  offset=match.start()-3
  if offset<0 or offset%4:continue
  word=struct.unpack_from('<I',text,offset)[0]
  if word&0xffe0001f==0xd4000001:svc.append(mem+offset)
  if word&0xffffffe0 in (0xd51bd040,0xd51bd060):msr.append(mem+offset)
 manifest['main']={'path':str(a.main),'sha256':value,'diagnostic_quota':a.quota_bytes,'stack_bytes':a.stack_bytes,
  'text_bytes':size,'direct_svc_offsets':svc,'direct_msr_tp_offsets':msr}
 if svc or msr:raise RuntimeError('reviewed main contains direct kernel/TP-writing instructions; profile cannot certify ownership')
 for name,arguments in [('nnmain',['--symbol','nnMain']),('malloc',['--symbol','malloc','4096'])]:
  run('host-original-'+name,[out/'nso_owned_startup.host',a.main,'--guest-quota',str(a.quota_bytes),'--guest-stack',str(a.stack_bytes),*arguments],133,10)
  diagnostic=(out/('host-original-'+name)/'stderr.log').read_text()
  boundary='index=71 name=_ZN2nn2os19AllocateMemoryPagesEmm' if name=='nnmain' else 'index=66 name=_ZN2nn2os36GetVirtualAddressMemoryResourceUsageEv'
  if '[query-port-policy]' not in diagnostic or boundary not in diagnostic:
   raise RuntimeError('original code did not reach the recorded post-Query page boundary')
 manifest['main']['sha256_after']=digest(a.main)
 if manifest['main']['sha256_after']!=value:raise RuntimeError('source main changed')
sysroot=tc/'sysroot';resource=tc/'lib/clang/18';libs=sysroot/'usr/lib/aarch64-linux-android';api=libs/'28'
official=json.loads((tc.parent/'validated-members.json').read_text())
if official['source']!='https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip':raise RuntimeError('unexpected NDK source')
known={v['path']:v for v in official['members']}
manifest['official_source']=official['source'];manifest['whole_archive_sha1_verified']=official['whole_archive_sha1_verified'];manifest['official_members']={}
library_inputs=[libs/'libc++abi.a',libs/'libc++_static.a',resource/'lib/linux/aarch64/libunwind.a',resource/'lib/linux/libclang_rt.builtins-aarch64-android.a']
for v in [*library_inputs,api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',sysroot/'usr/include/pthread.h',sysroot/'usr/include/sys/mman.h']:
 name=str(v.relative_to(tc));data=v.read_bytes();crc=f'{zlib.crc32(data):08x}'
 if len(data)!=known[name]['bytes'] or crc!=known[name]['crc32']:raise RuntimeError('official member mismatch '+name)
 manifest['official_members'][name]={'bytes':len(data),'crc32':crc,'sha256':digest(v)}
common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-resource-dir',resource,'-isystem',sysroot/'usr/include/aarch64-linux-android','-fPIC',*strict]
android=[]
for source in sources:
 obj=out/(source.stem+'.android.o');android.append(obj)
 run('android-compile-'+source.stem,['clang',*common,'-c',source,'-o',obj])
run('android-compile-contract',['clang',*common,'-c',test,'-o',out/'contract.android.o'])
run('android-compile-cli',['clang',*common,'-DNSO_WITH_HOST_HLE=1','-DNSO_WITH_GUEST_PROFILE=1','-c',cli,'-o',out/'cli.android.o'])
ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld'
for name,extra in [('guest_process_contract.android',out/'contract.android.o'),('nso_owned_startup.android',out/'cli.android.o')]:
 binary=out/name
 run('android-link-'+name,['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'-pie','--dynamic-linker','/system/bin/linker64',
  '--no-undefined','--eh-frame-hdr','-z','max-page-size=16384','-z','relro','-z','now','-z','noexecstack','--build-id=sha1',
  '-Map',out/(name+'.map'),'-o',binary,api/'crtbegin_dynamic.o',*android,extra,'--start-group',*library_inputs,'--end-group','-L',api,'-lc','-lm','-ldl',api/'crtend_android.o'])
 elf=run('android-inspect-'+name,[tc/'bin/llvm-readelf','--wide','--file-header','--program-headers','--sections','--dynamic','--symbols',binary])
 data=binary.read_bytes()
 if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise RuntimeError('not AArch64 ELF64')
 if 'nso_loader_test_vm' in elf or '.eh_frame_hdr' not in elf or 'GNU_EH_FRAME' not in elf:raise RuntimeError('production/unwind metadata mismatch')
 if 'libc++abi.a(cxa_guard.cpp.o)' not in (out/(name+'.map')).read_text():raise RuntimeError('guard not from official compiler runtime')
 manifest.setdefault('android_artifacts',{})[name]={'path':str(binary),'bytes':len(data),'sha256':digest(binary)}
for name,value in manifest['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed during build '+name)
for name,value in manifest['official_members'].items():
 if digest(tc/name)!=value['sha256']:raise RuntimeError('official input changed '+name)
save();print('Owned profile normal/recovery contracts passed; original post-Query boundary recorded if requested; Android NOT_RUN')
PY
