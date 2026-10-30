#!/bin/sh
# Run from this worktree root. Host contract plus an optional offline Android
# cross compile. No downloads, no game entry, no host TP writes, no device
# operations and no adb.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,json,platform,struct,subprocess
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Host guest TP mirror and trap contract, plus an optional Android cross compile')
p.add_argument('--out',type=Path,required=True)
p.add_argument('--android-sysroot',type=Path,help='optional existing NDK sysroot for an Android target compile')
a=p.parse_args();out=a.out.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must stay in this checkout .local')
source=ROOT/'src/runtime/hle/guest_tp.c';header=ROOT/'src/runtime/hle/guest_tp.h'
tls=ROOT/'src/runtime/hle/module_tls.c';test=ROOT/'tests/native/guest_tp_contract.c'
device_test=ROOT/'tests/native/guest_tp_device.c'
for v in (source,header,tls,test,device_test):
 if not v.is_file():p.error('run from the repository root')
out.mkdir(parents=True,exist_ok=False)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
inputs=[source,header,tls,test,device_test,ROOT/'scripts/runtime/build_guest_tp_contract.sh']
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
 'dirty':subprocess.check_output(['git','status','--short'],text=True),'host':platform.platform(),
 'architecture':platform.machine(),'compiler':subprocess.check_output(['clang','--version'],text=True),
 'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},'cases':{},
 'device_executed':False,'game_entry_executed':False,'host_tpidr_el0_written':False,
 'guest_tp_source':'static main module template copied per thread',
 'trap_encoding':'BRK #0x2a00 with the destination register in bits 9:5'}
def save():(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
def run(name,cmd):
 save()
 command=list(map(str,cmd));manifest['cases'][name]={'argv':command}
 r=subprocess.run(['python3',str(ROOT/'scripts/runtime/run_capture.py'),'--out',str(out/name),'--timeout','60','--',*command],capture_output=True,text=True)
 print(r.stdout.strip());entry=json.loads((out/name/'run.json').read_text())
 entry['argv']=command;manifest['cases'][name]=entry;save()
 if r.returncode or entry['timed_out']:
  print((out/name/'stderr.log').read_text());raise RuntimeError(name+' failed')
 return (out/name/'stdout.log').read_text()
strict=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-pedantic']
binary=out/'guest_tp_contract.host'
run('host-build',['clang',*strict,'-pthread',source,tls,test,'-o',binary])
log=run('host-run',[binary]);case=json.loads(log.splitlines()[-1])
manifest['host_contract']=case
if case['errors'] or case['live_mirrors']:raise RuntimeError('guest TP contract reported errors or a leaked mirror')
if case['threads']!=8 or case['observed_sites']!=5 or case['storage']!=0x1030:raise RuntimeError('guest TP contract coverage mismatch')
if case['trap_handler']!='NOT_RUN_HOST':raise RuntimeError('the host must not claim a device trap run')
if a.android_sysroot:
 sysroot=a.android_sysroot.resolve()
 if not (sysroot/'usr/include/ucontext.h').is_file():p.error('the sysroot has no Bionic ucontext.h')
 common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-fPIC','-fno-emulated-tls',*strict]
 obj=out/'guest_tp.android.o'
 run('android-compile',['clang',*common,'-c',source,'-o',obj])
 data=obj.read_bytes()
 if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<HH',data,16)!=(1,183):raise RuntimeError('cross object is not little-endian ELF64 AArch64 ET_REL')
 nm=sysroot.parent/'bin'/'llvm-nm'
 if not nm.is_file():
  nm=Path('/Library/Developer/CommandLineTools/usr/bin/llvm-nm')
 if not nm.is_file():p.error('the sysroot has no sibling llvm-nm and no system llvm-nm')
 symbols=subprocess.run([str(nm),'--defined-only',str(obj)],capture_output=True,text=True)
 if symbols.returncode:raise RuntimeError('llvm-nm failed on the Android object')
 names=[line.split()[-1] for line in symbols.stdout.splitlines() if line.strip()]
 if 'guest_tp_install_trap_handler' not in names:raise RuntimeError('the Android trap handler was not compiled')
 if any(name.startswith('guest_tp_test_') for name in names):raise RuntimeError('test hooks in the production object')
 if any(name.startswith('__emutls') for name in names):raise RuntimeError('emulated TLS would make the trap handler call libc')
 if 'guest_tp_thread_mirror' not in names:raise RuntimeError('the thread local mirror is not native TLS')
 manifest['android_object']={'path':str(obj),'bytes':len(data),'sha256':digest(obj),
  'symbols':sorted(names),'executed':False,
  'classification':'AppleClang Android target compile against the supplied NDK sysroot; not an NDK link and not a device execution'}
 device=ROOT/'tests/native/guest_tp_device.c'
 device_obj=out/'guest_tp_device.android.o'
 run('android-compile-device-test',['clang',*common,'-c',device,'-o',device_obj])
 device_bytes=device_obj.read_bytes()
 if device_bytes[:6]!=b'\x7fELF\x02\x01':raise RuntimeError('device test object is not ELF64')
 manifest['android_device_test']={'path':str(device_obj),'bytes':len(device_bytes),
  'sha256':digest(device_obj),'executed':False,'device_status':'NOT_RUN_DEVICE',
  'classification':'compile only; the coordinator owns the device run'}
for name,value in manifest['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed while building: '+name)
save()
print('guest TP mirror, per thread isolation, rewrite and trap decision contracts passed; device run NOT_RUN')
PY
