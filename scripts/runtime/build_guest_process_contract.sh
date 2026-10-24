#!/bin/sh
# Offline, explicit-policy host/Android checkpoint. No device operations.
set -eu
exec python3 - "$@" <<'PY'
import argparse,hashlib,importlib.util,json,platform,re,struct,subprocess,zlib
from pathlib import Path
ROOT=Path.cwd()
p=argparse.ArgumentParser(description='Build the restricted owned startup experiment')
p.add_argument('--out',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True)
p.add_argument('--pages-source-root',type=Path,default=ROOT,help='reviewed guest_pages component checkout, default this integrated tree')
p.add_argument('--boundary-source-root',type=Path,default=ROOT,help='frozen native_boundary component checkout')
p.add_argument('--os-tls-source-root',type=Path,default=ROOT,help='frozen guest_os_tls component checkout')
p.add_argument('--resource-quota-bytes',type=int,required=True,help='explicit logical metadata arena bytes, no default')
p.add_argument('--main',type=Path);p.add_argument('--quota-bytes',type=int);p.add_argument('--stack-bytes',type=int)
p.add_argument('--rust-root',type=Path,default=Path.home()/'.rustup/toolchains/stable-aarch64-apple-darwin')
a=p.parse_args();out=a.out.resolve();tc=a.toolchain.resolve()
if not out.is_relative_to((ROOT/'.local').resolve()):p.error('products must stay in this checkout .local')
if a.resource_quota_bytes<=0:p.error('resource quota must be positive')
if a.main and (not a.quota_bytes or not a.stack_bytes):p.error('main requires explicit diagnostic quota/stack inputs')
out.mkdir(parents=True,exist_ok=False)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
nso=ROOT/'src/runtime/nso';hle=ROOT/'src/runtime/hle'
core_sources=[nso/'nso_loader.c',*[hle/(s+'.c') for s in ['guest_context','guest_process','nso_host','address_region','mutex','exit_registry','module_tls','guest_tp']]]
pages_hle=a.pages_source_root.resolve()/'src/runtime/hle';pages_source=pages_hle/'guest_pages.c'
if not pages_source.is_file() or not pages_source.with_suffix('.h').is_file():p.error('guest_pages component missing; integrate its frozen commit or provide --pages-source-root')
sources=[*core_sources,pages_source]
boundary_nso=a.boundary_source_root.resolve()/'src/runtime/nso'
boundary_source=boundary_nso/'native_boundary.c';boundary_assembly=boundary_nso/'native_boundary.S'
if not all(v.is_file() for v in [boundary_source,boundary_assembly,boundary_source.with_suffix('.h')]):p.error('native_boundary component missing')
sources.append(boundary_source)
os_hle=a.os_tls_source_root.resolve()/'src/runtime/hle';os_source=os_hle/'guest_os_tls.c'
if not os_source.is_file() or not os_source.with_suffix('.h').is_file():p.error('guest_os_tls component missing')
sources.append(os_source)
test=ROOT/'tests/native/guest_process_contract.c';cli=nso/'nso_call.c'
inputs=[*core_sources,*[s.with_suffix('.h') for s in core_sources],test,cli,ROOT/'scripts/runtime/build_guest_process_contract.sh',ROOT/'tests/native/test_loader.py']
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
 'dirty':subprocess.check_output(['git','status','--short'],text=True),'host':platform.platform(),
 'compiler':subprocess.check_output(['clang','--version'],text=True),'cases':{},
 'source_sha256':{str(v.relative_to(ROOT)):digest(v) for v in inputs},'android_executed':False,
 'page_owner_source_root':str(a.pages_source_root.resolve()),
 'page_owner_inputs':{str(v):digest(v) for v in [pages_source,pages_source.with_suffix('.h')]},
 'page_owner_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.pages_source_root,text=True).strip(),
 'boundary_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.boundary_source_root,text=True).strip(),
 'boundary_inputs':{str(v):digest(v) for v in [boundary_source,boundary_assembly,boundary_source.with_suffix('.h')]},
 'os_tls_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.os_tls_source_root,text=True).strip(),
 'os_tls_inputs':{str(v):digest(v) for v in [os_source,os_source.with_suffix('.h')]},
 'quota_policy':'required explicit native logical backing cap; not original SVC measurements',
 'resource_policy':{'explicit_bytes':a.resource_quota_bytes,'kind':'bounded actual ticket/page/VA-node arena, not Horizon page tables or RSS','sdk_report_margin':524288},
 'heap_policy':'initialized immutable empty native heap; Vamm pages have a separate sole ticket owner',
 'lock_order':'process -> page owner -> region -> terminal Context, or owner -> Context; external raw APIs quiescent',
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
# Ordinary generated fixtures, no game instruction bytes. One exercises a guest
# x18 clobber across an admitted typed host import; one tests the init executor.
data=bytearray((out/'contract.nso').read_bytes())
names=b'\0nnutilReferSymbol\0missing_platform_call\0';data[0x1160:0x1160+len(names)]=names
struct.pack_into('<I',data,0x1130,names.index(b'missing_platform_call'));struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
code=[0xa9bf7bfd,0x910003fd,0xd2800032,0x90000050,0xf9410210,0xd63f0200,
      0xf100065f,0xd2801220,0x9a9f0000,0xa8c17bfd,0xd65f03c0]
struct.pack_into('<'+'I'*len(code),data,0x220,*code);(out/'boundary.nso').write_bytes(data)
data=bytearray((out/'contract.nso').read_bytes())
for i,(tag,value) in enumerate([(12,0x20),(25,0x8240),(27,16),(0,0)]):struct.pack_into('<QQ',data,0x20a0+i*16,tag,value)
struct.pack_into('<Q',data,0x2000+5*16+8,120)
for i,(target,addend) in enumerate([(0x8240,0x40),(0x8248,0x60)]):struct.pack_into('<QQq',data,0x1288+i*24,target,0x403,addend)
(out/'init-executor.nso').write_bytes(data)
data=bytearray((out/'contract.nso').read_bytes());struct.pack_into('<Q',data,0x1240,0x4200);(out/'readonly-import.nso').write_bytes(data)
manifest['generated_fixtures']={name:{'bytes':(out/name).stat().st_size,'sha256':digest(out/name)} for name in ['boundary.nso','init-executor.nso','readonly-import.nso']}
strict=['-std=c11','-O2','-g','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-I',pages_hle,'-I',boundary_nso,'-I',os_hle]
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
run('host-boundary-contract',[out/'guest_process_contract.host','--boundary',out/'boundary.nso'])
run('host-retarget-contract',[out/'guest_process_contract.host','--retarget',out/'init-executor.nso'])
run('host-readonly-retarget-contract',[out/'guest_process_contract.host','--retarget-readonly',out/'readonly-import.nso'])
run('host-arguments-contract',[out/'guest_process_contract.host','--arguments',out/'contract.nso'])
run('host-rwlock-contract',[out/'guest_process_contract.host','--rwlock'])
run('host-uuid-contract',[out/'guest_process_contract.host','--uuid'])
run('host-time-contract',[out/'guest_process_contract.host','--time'])
run('host-tick-contract',[out/'guest_process_contract.host','--tick'])
run('host-ctype-contract',[out/'guest_process_contract.host','--ctype'])
run('host-guest-tp-contract',[out/'guest_process_contract.host','--guest-tp'])
run('host-threads-contract',[out/'guest_process_contract.host','--threads'])
run('host-fs-allocator-contract',[out/'guest_process_contract.host','--fs-allocator'])
run('host-fs-query-cache-contract',[out/'guest_process_contract.host','--fs-query-cache'])
run('host-fs-mount-rom-contract',[out/'guest_process_contract.host','--fs-mount-rom'])
run('host-fs-open-file-contract',[out/'guest_process_contract.host','--fs-open-file'])
run('host-aoc-contract',[out/'guest_process_contract.host','--aoc'])
run('host-nifm-contract',[out/'guest_process_contract.host','--nifm'])
run('host-socket-contract',[out/'guest_process_contract.host','--socket'])
run('host-socket-eventfd-contract',[out/'guest_process_contract.host','--socket-eventfd'])
run('host-account-contract',[out/'guest_process_contract.host','--account'])
run('host-oe-contract',[out/'guest_process_contract.host','--oe'])
run('host-gettimeofday-contract',[out/'guest_process_contract.host','--gettimeofday'])
run('host-os-process-handle-contract',[out/'guest_process_contract.host','--os-process-handle'])
run('host-nn-result-contract',[out/'guest_process_contract.host','--nn-result'])
run('host-stdio-contract',[out/'guest_process_contract.host','--stdio'])
run('host-fs-open-dir-contract',[out/'guest_process_contract.host','--fs-open-dir'])
run('host-os-event-contract',[out/'guest_process_contract.host','--os-event'])
run('host-posix-memalign-contract',[out/'guest_process_contract.host','--posix-memalign'])
run('host-cxa-thread-atexit-contract',[out/'guest_process_contract.host','--cxa-thread-atexit'])
run('host-diag-abort-contract',[out/'guest_process_contract.host','--diag-abort'])
run('host-diag-aoc-event-contract',[out/'guest_process_contract.host','--diag-aoc-event'])
run('host-logf-contract',[out/'guest_process_contract.host','--logf'])
run('host-strcasecmp-contract',[out/'guest_process_contract.host','--strcasecmp'])
run('host-va-enabled-contract',[out/'guest_process_contract.host','--va-enabled',out/'contract.nso'])
run('host-invalid-va-enabled',[out/'guest_process_contract.host','--invalid-va-enabled'],134)
if '[va-capability-invalid]' not in (out/'host-invalid-va-enabled/stderr.log').read_text():raise RuntimeError('invalid capability query did not fail strictly')
run('host-locale-contract',[out/'guest_process_contract.host','--locale'])
run('host-resource-owner-contract',[out/'guest_process_contract.host','--resource-owner'])
run('host-resource-process-contract',[out/'guest_process_contract.host','--resource',out/'contract.nso'])
run('host-compile-resource-recovery',['clang',*strict,'-DGUEST_RESOURCE_TESTING=1','-c',hle/'guest_context.c','-o',out/'guest_context.resource-recovery.host.o'])
run('host-compile-resource-vm-recovery',['clang',*strict,'-DNSO_OWNED_PAGES_TESTING=1','-c',hle/'address_region.c','-o',out/'address_region.resource-recovery.host.o'])
resource_host=[v for v in host if v.name not in ['guest_context.host.o','address_region.host.o']]+[out/'address_region.resource-recovery.host.o']
run('host-link-resource-recovery',['clang',*strict,'-DGUEST_RESOURCE_TESTING=1','-DNSO_OWNED_PAGES_TESTING=1',test,*resource_host,out/'guest_context.resource-recovery.host.o','-lc++','-lpthread','-o',out/'guest_process_resource_recovery.host'])
run('host-resource-recovery',[out/'guest_process_resource_recovery.host','--resource-recovery'])
run('host-os-tls-contract',[out/'guest_process_contract.host','--os-tls',out/'contract.nso'])
run('host-tls-registration',[out/'guest_process_contract.host','--tls-registration',out/'contract.nso'])
run('host-tls-registration-fresh',[out/'guest_process_contract.host','--tls-registration-fresh',out/'contract.nso'])
run('host-dso-registration',[out/'guest_process_contract.host','--dso-registration',out/'contract.nso'])
run('host-snapshot-thread-contract',[out/'guest_process_contract.host','--snapshot-thread',out/'contract.nso'])
run('host-tls-HOST-null',[out/'guest_process_contract.host','--tls-host-null'])
run('host-tls-HOST-invalid',[out/'guest_process_contract.host','--tls-host-invalid'],134)
run('host-primary-identity',[out/'guest_process_contract.host','--identity',out/'contract.nso'])
run('host-compile-identity-recovery',['clang',*strict,'-DNSO_WITH_CXX_GUARDS=1','-DNSO_PROFILE_IDENTITY_TESTING=1','-c',hle/'guest_process.c','-o',out/'guest_process.identity-recovery.host.o'])
run('host-link-identity-recovery',['clang',*strict,'-DNSO_PROFILE_IDENTITY_TESTING=1',test,*[v for v in host if v.name!='guest_process.host.o'],out/'guest_process.identity-recovery.host.o','-lc++','-lpthread','-o',out/'guest_process_identity_recovery.host'])
run('host-identity-recovery',[out/'guest_process_identity_recovery.host','--identity-recovery',out/'contract.nso'])
for case in ['null','repeat']:
 run('host-tls-registration-'+case,[out/'guest_process_contract.host','--tls-registration-'+case,out/'contract.nso'],134)
 if '[tls-allocator-invalid]' not in (out/('host-tls-registration-'+case)/'stderr.log').read_text():raise RuntimeError('allocator registration must fail strictly')
run('host-os-tls-unsupported-provider',[out/'guest_process_contract.host','--tls-unsupported',out/'boundary.nso'],134)
unsupported=(out/'host-os-tls-unsupported-provider/stderr.log').read_text()
if '[os-tls-provider-fatal]' not in unsupported or 'value=0x0' not in unsupported or '[os-tls-cleanup]' in unsupported:raise RuntimeError('unsupported destructor provider did not failstop before cleanup')
run('host-build-loader-recovery',['clang',*strict,'-DNSO_LOADER_TESTING=1','-c',sources[0],'-o',out/'nso_loader.recovery.o'])
run('host-build-recovery',['clang',*strict,'-DNSO_LOADER_TESTING=1',test,out/'nso_loader.recovery.o',*host[1:],'-lc++','-lpthread','-o',out/'guest_process_recovery.host'])
log=run('host-contract-recovery',[out/'guest_process_recovery.host',out/'contract.nso'])
manifest['recovery_contract']=json.loads(log.splitlines()[-1])
if manifest['recovery_contract']['errors']:raise RuntimeError('recovery contract failed')
run('host-compile-argument-recovery',['clang',*strict,'-DNSO_WITH_CXX_GUARDS=1','-DNSO_PROFILE_ARGUMENT_TESTING=1','-c',hle/'guest_process.c','-o',out/'guest_process.argument-recovery.host.o'])
argument_host=[v for v in host if v.name!='guest_process.host.o']
run('host-link-argument-recovery',['clang',*strict,'-DNSO_PROFILE_ARGUMENT_TESTING=1',test,*argument_host,out/'guest_process.argument-recovery.host.o','-lc++','-lpthread','-o',out/'guest_process_argument_recovery.host'])
run('host-argument-recovery',[out/'guest_process_argument_recovery.host','--arguments-recovery',out/'contract.nso'])
run('host-invalid-query',[out/'guest_process_contract.host','--invalid-query'],134)
if '[query-invalid]' not in (out/'host-invalid-query/stderr.log').read_text():raise RuntimeError('missing fatal invalid Query diagnostic')
run('host-build-cli',['clang',*strict,'-DNSO_WITH_HOST_HLE=1','-DNSO_WITH_GUEST_PROFILE=1',cli,*host,'-lc++','-lpthread','-o',out/'nso_owned_startup.host'])
legacy=[v for v in host if v.stem not in ['guest_context.host','guest_process.host','guest_pages.host','native_boundary.host','guest_os_tls.host']]
run('host-build-legacy-cli',['clang',*strict,'-DNSO_WITH_HOST_HLE=1',cli,*legacy,'-lc++','-lpthread','-o',out/'nso_legacy.host'])
run('host-legacy-info',[out/'nso_legacy.host',out/'contract.nso','--info'])
run('host-profile-info',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1048576','--guest-stack','65536','--info'])
run('host-profile-explicit-arguments',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1048576','--guest-stack','65536','--guest-arg','ffrs','--guest-arg','','--guest-arg','option=123','--info'])
run('host-profile-arguments-require-policy',[out/'nso_owned_startup.host',out/'contract.nso','--guest-arg','ffrs','--info'],2)
run('host-profile-explicit-resources',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1048576','--guest-stack','65536','--guest-resource-quota',str(a.resource_quota_bytes),'--info'])
run('host-profile-resource-zero',[out/'nso_owned_startup.host',out/'contract.nso','--guest-quota','1048576','--guest-stack','65536','--guest-resource-quota','0','--info'],2)
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
 manifest['main']={'path':str(a.main),'sha256':value,'diagnostic_quota':a.quota_bytes,'stack_bytes':a.stack_bytes,'resource_quota':a.resource_quota_bytes,
  'text_bytes':size,'direct_svc_offsets':svc,'direct_msr_tp_offsets':msr}
 if svc or msr:raise RuntimeError('reviewed main contains direct kernel/TP-writing instructions; profile cannot certify ownership')
 for name,arguments in [('nnmain',['--symbol','nnMain']),('malloc',['--symbol','malloc','4096']),('startup',['--symbol','nninitStartup']),('startup-arguments',['--guest-arg','ffrs','--guest-arg','option=123','--symbol','nninitStartup']),('crt',['--init-wrapper','--info'])]:
  run('host-original-'+name,[out/'nso_owned_startup.host',a.main,'--guest-quota',str(a.quota_bytes),'--guest-stack',str(a.stack_bytes),'--guest-resource-quota',str(a.resource_quota_bytes),*arguments],1,10)
  diagnostic=(out/('host-original-'+name)/'stderr.log').read_text()
  if '[boundary-error] scope-init status=1; no guest execution' not in diagnostic or '[query-port-policy]' in diagnostic:
   raise RuntimeError('unsupported Mac executed guest code or missed its explicit boundary failure')
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
common=['--target=aarch64-linux-android28','--sysroot',sysroot,'-resource-dir',resource,'-isystem',sysroot/'usr/include/aarch64-linux-android','-fPIC','-ffixed-x18','-fno-emulated-tls',*strict]
ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld'
# Self-authored register-ABI guest contract; no game instructions. The callbacks
# use only typed OS imports and Query, including zero-value/reentrant cleanup.
assembly=out/'os-tls-fixture.S'
assembly.write_text(r'''.text
.macro import_call offset
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16, #\offset]
 blr x16
.endm
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 str x19, [x20]
 mov x18, #0x55
 add x0, x20, #8
 adr x1, callback_a
 import_call 0
 cbnz w0, entry_error
 add x0, x20, #12
 adr x1, callback_b
 import_call 0
 cbnz w0, entry_error
 ldr w0, [x20, #8]
 import_call 8
 str x0, [x19]
 ldr w0, [x20, #12]
 mov x1, #7
 import_call 16
 add x0, x20, #16
 mov x1, #0
 import_call 0
 cbnz w0, entry_error
 ldr w0, [x20, #16]
 mov x1, #0x99
 import_call 16
 ldr w0, [x20, #16]
 import_call 24
 ldr w0, [x20, #16]
 import_call 8
 str x0, [x19, #8]
 add x0, x20, #16
 mov x1, #0
 import_call 0
 cbnz w0, entry_error
 ldr w0, [x20, #16]
 import_call 8
 str x0, [x19, #16]
 ldr w0, [x20, #16]
 import_call 24
 mov x9, #1
 str x9, [x19, #104]
 mov x0, #0x91
 b entry_return
entry_error:
 mov x9, #1
 str x9, [x19, #96]
 mov x0, #0
entry_return:
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.balign 16
callback_a:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 ldr x19, [x20]
 ldr x9, [x19, #24]
 cbnz x9, a_second
 str x0, [x19, #40]
 b a_record
a_second:
 str x0, [x19, #48]
a_record:
 add x9, x9, #1
 str x9, [x19, #24]
 ldr x10, [x19, #88]
 lsl x10, x10, #4
 orr x10, x10, #1
 str x10, [x19, #88]
 cmp x9, #1
 b.ne a_return
 ldr w0, [x20, #8]
 mov x1, #77
 import_call 16
 ldr w0, [x20, #12]
 mov x1, #9
 import_call 16
a_return:
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.balign 16
callback_b:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 ldr x19, [x20]
 str x0, [x19, #56]
 ldr x9, [x19, #32]
 add x9, x9, #1
 str x9, [x19, #32]
 ldr x10, [x19, #88]
 lsl x10, x10, #4
 orr x10, x10, #2
 str x10, [x19, #88]
 ldr w0, [x20, #8]
 import_call 8
 str x0, [x19, #64]
 ldr w0, [x20, #8]
 mov x1, #5
 import_call 16
 add x0, x20, #32
 import_call 32
 ldr w9, [x20, #80]
 str x9, [x19, #72]
 ldr x9, [x20, #72]
 str x9, [x19, #80]
 import_call 40
 str x0, [x19, #112]
 import_call 48
 str x0, [x19, #120]
 ldr x9, [x0, #8]
 ldrb w9, [x9]
 str x9, [x19, #128]
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 56
.section .fixture_state,"aw"
.global fixture_state
fixture_state: .zero 112
''')
run('fixture-os-assemble',['clang',*common,'-c',assembly,'-o',out/'os-tls-fixture.o'])
run('fixture-os-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_state=0x8280','-e','fixture_entry','--oformat=binary','-o',out/'os-tls-fixture.raw',out/'os-tls-fixture.o'])
raw=(out/'os-tls-fixture.raw').read_bytes()
if len(raw)>0x8300:raise RuntimeError('unexpected fixture extent')
data=bytearray((out/'contract.nso').read_bytes());data[0x1000:0x1400]=bytes(0x400);data[0x2000:0x2400]=bytes(0x400)
data[0x1000:0x1004]=b'MOD0';struct.pack_into('<i',data,0x1004,0x4000)
struct.pack_into('<I',data,0x18,0x400);data[0x200:0x600]=raw[:0x400].ljust(0x400,b'\0')
struct.pack_into('<I',data,0x204,0x4000)
symbols=['_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE','_ZN2nn2os11GetTlsValueENS0_7TlsSlotE','_ZN2nn2os11SetTlsValueENS0_7TlsSlotEm','_ZN2nn2os11FreeTlsSlotENS0_7TlsSlotE','_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE','_ZN2nn2os11GetHostArgcEv','_ZN2nn2os11GetHostArgvEv']
names=bytearray(b'\0')
for i,name in enumerate(symbols,1):
 offset=len(names);names+=name.encode()+b'\0';struct.pack_into('<IBBHQQ',data,0x1100+i*24,offset,0x12,0,0,0,0)
 struct.pack_into('<QQq',data,0x1300+(i-1)*24,0x8200+(i-1)*8,(i<<32)|0x402,0)
if len(names)>0x140:raise RuntimeError('fixture strings overlap relocation table')
data[0x11c0:0x11c0+len(names)]=names
struct.pack_into('<II',data,0x1080,1,8)
tags=[(4,0x4080),(6,0x4100),(5,0x41c0),(10,len(names)),(7,0x4300),(8,168),(9,24),(0,0)]
for i,(tag,value) in enumerate(tags):struct.pack_into('<QQ',data,0x2000+i*16,tag,value)
(out/'os-tls-callback.nso').write_bytes(data)
manifest['generated_fixtures']['os-tls-callback.nso']={'bytes':len(data),'sha256':digest(out/'os-tls-callback.nso'),'kind':'self-authored scalar-register fixture, not game bytes','assembly_sha256':digest(assembly)}
# Getter fixture uses a complete caller-selected vector and clobbers x18 while
# crossing both real host getters. It is self-authored, not extracted code.
argv_assembly=out/'arguments-fixture.S'
argv_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov x18, #0x55
 adrp x20, fixture_got
 add x20, x20, :lo12:fixture_got
 ldr x16, [x20]
 blr x16
 str x0, [x19]
 ldr x16, [x20, #8]
 blr x16
 str x0, [x19, #8]
 str x18, [x19, #16]
 mov x0, #0x91
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 16
''')
run('fixture-arguments-assemble',['clang',*common,'-c',argv_assembly,'-o',out/'arguments-fixture.o'])
run('fixture-arguments-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'arguments-fixture.raw',out/'arguments-fixture.o'])
raw=(out/'arguments-fixture.raw').read_bytes()
data=bytearray((out/'contract.nso').read_bytes());data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2os11GetHostArgcEv\0_ZN2nn2os11GetHostArgvEv\0'
data[0x1160:0x1160+len(names)]=names
struct.pack_into('<I',data,0x1130,names.index(b'_ZN2nn2os11GetHostArgvEv'))
struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,48)
struct.pack_into('<QQq',data,0x1258,0x8208,(2<<32)|0x402,0)
(out/'arguments.nso').write_bytes(data)
manifest['generated_fixtures']['arguments.nso']={'bytes':len(data),'sha256':digest(out/'arguments.nso'),'kind':'self-authored two-getter native boundary contract','assembly_sha256':digest(argv_assembly)}
run('host-arguments-native-unsupported',[out/'guest_process_contract.host','--arguments-native',out/'arguments.nso'])
resource_assembly=out/'resource-fixture.S'
resource_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov x18, #0x55
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 stp x0, x1, [x19]
 str x18, [x19, #16]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-resource-assemble',['clang',*common,'-c',resource_assembly,'-o',out/'resource-fixture.o'])
run('fixture-resource-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'resource-fixture.raw',out/'resource-fixture.o'])
raw=(out/'resource-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2os36GetVirtualAddressMemoryResourceUsageEv\0'
data[0x1160:0x1160+len(names)]=names
struct.pack_into('<II',data,0x1080,1,2);struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,24)
(out/'resource.nso').write_bytes(data)
manifest['generated_fixtures']['resource.nso']={'bytes':len(data),'sha256':digest(out/'resource.nso'),'kind':'self-authored dual GP return ABI fixture','assembly_sha256':digest(resource_assembly)}
run('host-resource-native-unsupported',[out/'guest_process_contract.host','--resource-native',out/'resource.nso'])
# movie::SetAllocator fixture: the guest calls the import with its own three
# allocator callbacks plus a NULL userdata. The callbacks live at fixed module
# offsets so the contract can prove the four arguments reached the host stub
# unmoved. A missing import_signature entry makes prepare_boundary_locked()
# refuse the whole boundary, so this fixture also exercises that gate.
movie_assembly=out/'movie-allocator-fixture.S'
movie_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov x18, #0x55
 adr x0, cb_alloc
 adr x1, cb_free
 adr x2, cb_realloc
 mov x3, xzr
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 mov x9, #1
 str x9, [x19]
 str x18, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0x80
.global cb_alloc
cb_alloc: ret
.global cb_free
cb_free: ret
.global cb_realloc
cb_realloc: ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-movie-allocator-assemble',['clang',*common,'-c',movie_assembly,'-o',out/'movie-allocator-fixture.o'])
run('fixture-movie-allocator-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'movie-allocator-fixture.raw',out/'movie-allocator-fixture.o'])
raw=(out/'movie-allocator-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_\0'
data[0x1160:0x1160+len(names)]=names
# DT_RELASZ (dynamic index 5) is set to 24 on purpose: this fixture keeps one
# dynsym entry, so the base table's third RELA (R_GLOB_DAT against symbol 2)
# would be out of bounds and must not be applied. Dynamic index 5 is
# DT_RELASZ, not DT_RELAENT (index 6) - the base already carries 24 there.
struct.pack_into('<II',data,0x1080,1,2);struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,24)
(out/'movie-allocator.nso').write_bytes(data)
manifest['generated_fixtures']['movie-allocator.nso']={'bytes':len(data),'sha256':digest(out/'movie-allocator.nso'),'kind':'self-authored four-argument void movie allocator registration fixture','assembly_sha256':digest(movie_assembly)}
run('host-movie-allocator-contract',[out/'guest_process_contract.host','--movie-allocator',out/'movie-allocator.nso'])
movie_stderr=(out/'host-movie-allocator-contract'/'stderr.log').read_text()
if '[hle] movie::SetAllocator:' not in movie_stderr:raise RuntimeError('movie::SetAllocator stub did not run')
if '[boundary-unsupported]' in movie_stderr:raise RuntimeError('movie::SetAllocator import signature was refused by the boundary')
# nn::oe::SetCpuBoostMode (main NSO import index 75) takes one enum argument and
# returns nothing. The fixture calls the import with mode 1 through the real
# guest import path, so the boundary must accept the one-argument void
# signature or the guest never runs. A missing import_signature entry makes
# prepare_boundary_locked() refuse the whole boundary.
oe_assembly=out/'oe-cpu-boost-fixture.S'
oe_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov w0, #1
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 mov x9, #1
 str x9, [x19]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-oe-cpu-boost-assemble',['clang',*common,'-c',oe_assembly,'-o',out/'oe-cpu-boost-fixture.o'])
run('fixture-oe-cpu-boost-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'oe-cpu-boost-fixture.raw',out/'oe-cpu-boost-fixture.o'])
raw=(out/'oe-cpu-boost-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE\0'
data[0x1160:0x1160+len(names)]=names
struct.pack_into('<II',data,0x1080,1,2);struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,24)
(out/'oe-cpu-boost.nso').write_bytes(data)
manifest['generated_fixtures']['oe-cpu-boost.nso']={'bytes':len(data),'sha256':digest(out/'oe-cpu-boost.nso'),'kind':'self-authored one-argument void nn::oe cpu boost mode fixture','assembly_sha256':digest(oe_assembly)}
run('host-oe-cpu-boost-contract',[out/'guest_process_contract.host','--oe-cpu-boost',out/'oe-cpu-boost.nso'])
oe_stderr=(out/'host-oe-cpu-boost-contract'/'stderr.log').read_text()
if '[hle] nn::oe::SetCpuBoostMode:' not in oe_stderr:raise RuntimeError('nn::oe::SetCpuBoostMode stub did not run')
if '[boundary-unsupported]' in oe_stderr:raise RuntimeError('nn::oe::SetCpuBoostMode import signature was refused by the boundary')
# nn::hid::InitializeKeyboard() and nn::hid::InitializeMouse() (main NSO import
# indexes 234 and 235) take no arguments and return nothing. The fixture calls
# both imports through the real guest import path, so the boundary must accept
# the zero-argument void signature or the guest never runs. Two imports need two
# dynsym entries and two GOT slots: the second RELA is rewritten from RELATIVE
# to ABS64 against symbol 2.
hid_assembly=out/'hid-init-fixture.S'
hid_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 adrp x16, fixture_got2
 add x16, x16, :lo12:fixture_got2
 ldr x16, [x16]
 blr x16
 mov x9, #1
 str x9, [x19]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
.section .fixture_got2,"aw"
.global fixture_got2
fixture_got2: .zero 8
''')
run('fixture-hid-init-assemble',['clang',*common,'-c',hid_assembly,'-o',out/'hid-init-fixture.o'])
run('fixture-hid-init-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_got2=0x8208','-e','fixture_entry','--oformat=binary','-o',out/'hid-init-fixture.raw',out/'hid-init-fixture.o'])
raw=(out/'hid-init-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn3hid18InitializeKeyboardEv\0_ZN2nn3hid15InitializeMouseEv\0'
data[0x1160:0x1160+len(names)]=names
# Three dynsym entries here, so the base RELA table (DT_RELASZ = 72, three
# entries) stays intact: dynamic index 5 is DT_RELASZ, not DT_RELAENT, and
# truncating it would drop the relocation that fills the second GOT slot.
struct.pack_into('<II',data,0x1080,1,3);struct.pack_into('<I',data,0x1130,names.index(b'_ZN2nn3hid15InitializeMouseEv'))
struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
struct.pack_into('<QQq',data,0x1258,0x8208,(2<<32)|0x101,0)
(out/'hid-init.nso').write_bytes(data)
manifest['generated_fixtures']['hid-init.nso']={'bytes':len(data),'sha256':digest(out/'hid-init.nso'),'kind':'self-authored two-import zero-argument void nn::hid keyboard/mouse initialisation fixture','assembly_sha256':digest(hid_assembly)}
run('host-hid-init-contract',[out/'guest_process_contract.host','--hid-init',out/'hid-init.nso'])
hid_stderr=(out/'host-hid-init-contract'/'stderr.log').read_text()
if '[hle] nn::hid::InitializeKeyboard:' not in hid_stderr:raise RuntimeError('nn::hid::InitializeKeyboard stub did not run')
if '[hle] nn::hid::InitializeMouse:' not in hid_stderr:raise RuntimeError('nn::hid::InitializeMouse stub did not run')
if '[boundary-unsupported]' in hid_stderr:raise RuntimeError('nn::hid initialisation import signature was refused by the boundary')
# nn::oe focus and notification setup (main NSO import indexes 222 to 226) is
# called by the guest from one straight-line sequence, so all five symbols
# must be bound. The fixture drives the two shapes that matter through the
# real guest import path: the zero-argument function that returns a value
# (222) and the one-argument setter (225). The boundary checks the signature
# of every bound import before the guest runs, so a successful guest call also
# proves the other three signatures were accepted.
focus_assembly=out/'oe-focus-fixture.S'
focus_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x17, [x16]
 blr x17
 uxtw x9, w0
 str x9, [x19]
 adrp x16, fixture_got2
 add x16, x16, :lo12:fixture_got2
 ldr x17, [x16]
 mov w0, #2
 blr x17
 mov x9, #1
 str x9, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
.section .fixture_got2,"aw"
.global fixture_got2
fixture_got2: .zero 8
''')
run('fixture-oe-focus-assemble',['clang',*common,'-c',focus_assembly,'-o',out/'oe-focus-fixture.o'])
run('fixture-oe-focus-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_got2=0x8208','-e','fixture_entry','--oformat=binary','-o',out/'oe-focus-fixture.raw',out/'oe-focus-fixture.o'])
raw=(out/'oe-focus-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2oe20GetCurrentFocusStateEv\0_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE\0'
data[0x1160:0x1160+len(names)]=names
struct.pack_into('<II',data,0x1080,1,3);struct.pack_into('<I',data,0x1130,names.index(b'_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE'))
struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
struct.pack_into('<QQq',data,0x1258,0x8208,(2<<32)|0x101,0)
(out/'oe-focus.nso').write_bytes(data)
manifest['generated_fixtures']['oe-focus.nso']={'bytes':len(data),'sha256':digest(out/'oe-focus.nso'),'kind':'self-authored two-import nn::oe focus state and focus handling mode fixture','assembly_sha256':digest(focus_assembly)}
run('host-oe-focus-contract',[out/'guest_process_contract.host','--oe-focus',out/'oe-focus.nso'])
focus_stderr=(out/'host-oe-focus-contract'/'stderr.log').read_text()
if '[hle] nn::oe::SetPerformanceModeChangedNotificationEnabled:' not in focus_stderr:raise RuntimeError('nn::oe::SetPerformanceModeChangedNotificationEnabled stub did not run')
if '[hle] nn::oe::SetOperationModeChangedNotificationEnabled:' not in focus_stderr:raise RuntimeError('nn::oe::SetOperationModeChangedNotificationEnabled stub did not run')
if '[hle] nn::oe::SetFocusHandlingMode:' not in focus_stderr:raise RuntimeError('nn::oe::SetFocusHandlingMode stub did not run')
if '[hle] nn::oe::SetResumeNotificationEnabled:' not in focus_stderr:raise RuntimeError('nn::oe::SetResumeNotificationEnabled stub did not run')
if '[boundary-unsupported]' in focus_stderr:raise RuntimeError('nn::oe focus import signature was refused by the boundary')
# nn::oe::GetOperationMode (main NSO import index 227) takes no argument and
# returns the 32-bit operation mode. The fixture calls the import through the
# real guest import path and keeps the returned word, so the boundary must
# accept the zero-argument signature with a return value or the guest never
# runs. A missing import_signature entry makes prepare_boundary_locked()
# refuse the whole boundary, so this fixture also exercises that gate.
operation_mode_assembly=out/'oe-operation-mode-fixture.S'
operation_mode_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x17, [x16]
 blr x17
 uxtw x9, w0
 str x9, [x19]
 mov x9, #1
 str x9, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-oe-operation-mode-assemble',['clang',*common,'-c',operation_mode_assembly,'-o',out/'oe-operation-mode-fixture.o'])
run('fixture-oe-operation-mode-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'oe-operation-mode-fixture.raw',out/'oe-operation-mode-fixture.o'])
raw=(out/'oe-operation-mode-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2oe16GetOperationModeEv\0'
data[0x1160:0x1160+len(names)]=names
# One dynsym entry here, so the base RELA table (DT_RELASZ = 72, three
# entries) is truncated to 24 bytes and only the first RELA stays: the two
# later ones point at symbol 2, which this fixture does not define. Dynamic
# index 5 is DT_RELASZ, not DT_RELAENT (index 6) - the base already carries
# 24 there.
struct.pack_into('<II',data,0x1080,1,2);struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,24)
(out/'oe-operation-mode.nso').write_bytes(data)
manifest['generated_fixtures']['oe-operation-mode.nso']={'bytes':len(data),'sha256':digest(out/'oe-operation-mode.nso'),'kind':'self-authored single-import zero-argument nn::oe operation mode fixture','assembly_sha256':digest(operation_mode_assembly)}
run('host-oe-operation-mode-contract',[out/'guest_process_contract.host','--oe-operation-mode',out/'oe-operation-mode.nso'])
operation_mode_stderr=(out/'host-oe-operation-mode-contract'/'stderr.log').read_text()
if '[hle] nn::oe::GetOperationMode:' not in operation_mode_stderr:raise RuntimeError('nn::oe::GetOperationMode stub did not run')
if '[boundary-unsupported]' in operation_mode_stderr:raise RuntimeError('nn::oe operation mode import signature was refused by the boundary')
# nn::ldn::GetState (main NSO import index 88) takes no argument and returns
# the 32-bit local wireless state. The fixture calls the import through the
# real guest import path and keeps the returned word, so the boundary must
# accept the zero-argument signature with a return value or the guest never
# runs. A missing import_signature entry makes prepare_boundary_locked()
# refuse the whole boundary, so this fixture also exercises that gate.
ldn_state_assembly=out/'ldn-get-state-fixture.S'
ldn_state_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x17, [x16]
 blr x17
 uxtw x9, w0
 str x9, [x19]
 mov x9, #1
 str x9, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-ldn-get-state-assemble',['clang',*common,'-c',ldn_state_assembly,'-o',out/'ldn-get-state-fixture.o'])
run('fixture-ldn-get-state-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'ldn-get-state-fixture.raw',out/'ldn-get-state-fixture.o'])
raw=(out/'ldn-get-state-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn3ldn8GetStateEv\0'
data[0x1160:0x1160+len(names)]=names
# One dynsym entry here, so the base RELA table (DT_RELASZ = 72, three
# entries) is truncated to 24 bytes and only the first RELA stays: the two
# later ones point at symbol 2, which this fixture does not define. Dynamic
# index 5 is DT_RELASZ, not DT_RELAENT (index 6) - the base already carries
# 24 there.
struct.pack_into('<II',data,0x1080,1,2);struct.pack_into('<Q',data,0x2000+3*16+8,len(names));struct.pack_into('<Q',data,0x2000+5*16+8,24)
(out/'ldn-get-state.nso').write_bytes(data)
manifest['generated_fixtures']['ldn-get-state.nso']={'bytes':len(data),'sha256':digest(out/'ldn-get-state.nso'),'kind':'self-authored single-import zero-argument nn::ldn local wireless state fixture','assembly_sha256':digest(ldn_state_assembly)}
run('host-ldn-get-state-contract',[out/'guest_process_contract.host','--ldn-get-state',out/'ldn-get-state.nso'])
ldn_state_stderr=(out/'host-ldn-get-state-contract'/'stderr.log').read_text()
if '[hle] nn::ldn::GetState:' not in ldn_state_stderr:raise RuntimeError('nn::ldn::GetState stub did not run')
if '[boundary-unsupported]' in ldn_state_stderr:raise RuntimeError('nn::ldn local wireless state import signature was refused by the boundary')
# nn::oe performance configuration (main NSO import indexes 239 and 240): the
# setter takes a mode and a clock and returns nothing, the getter returns the
# mode. The fixture drives the real guest import path for the round trip the
# guest itself performs - set a configuration, then read the mode back - so
# the boundary has to accept both signatures or the guest never runs.
performance_assembly=out/'oe-performance-fixture.S'
performance_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x17, [x16]
 mov w0, #5
 mov w1, #3
 movk w1, #2, lsl #16
 blr x17
 adrp x16, fixture_got2
 add x16, x16, :lo12:fixture_got2
 ldr x17, [x16]
 blr x17
 uxtw x9, w0
 str x9, [x19]
 mov x9, #1
 str x9, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
.section .fixture_got2,"aw"
.global fixture_got2
fixture_got2: .zero 8
''')
run('fixture-oe-performance-assemble',['clang',*common,'-c',performance_assembly,'-o',out/'oe-performance-fixture.o'])
run('fixture-oe-performance-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_got2=0x8208','-e','fixture_entry','--oformat=binary','-o',out/'oe-performance-fixture.raw',out/'oe-performance-fixture.o'])
raw=(out/'oe-performance-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi\0_ZN2nn2oe18GetPerformanceModeEv\0'
data[0x1160:0x1160+len(names)]=names
# Three dynsym entries here, so the base RELA table (DT_RELASZ = 72, three
# entries) stays intact: dynamic index 5 is DT_RELASZ, not DT_RELAENT, and
# truncating it would drop the relocation that fills the second GOT slot.
struct.pack_into('<II',data,0x1080,1,3);struct.pack_into('<I',data,0x1130,names.index(b'_ZN2nn2oe18GetPerformanceModeEv'))
struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
struct.pack_into('<QQq',data,0x1258,0x8208,(2<<32)|0x101,0)
(out/'oe-performance.nso').write_bytes(data)
manifest['generated_fixtures']['oe-performance.nso']={'bytes':len(data),'sha256':digest(out/'oe-performance.nso'),'kind':'self-authored two-import nn::oe performance configuration round-trip fixture','assembly_sha256':digest(performance_assembly)}
run('host-oe-performance-contract',[out/'guest_process_contract.host','--oe-performance',out/'oe-performance.nso'])
performance_stderr=(out/'host-oe-performance-contract'/'stderr.log').read_text()
if '[hle] nn::oe::SetPerformanceConfiguration:' not in performance_stderr:raise RuntimeError('nn::oe::SetPerformanceConfiguration stub did not run')
if '[boundary-unsupported]' in performance_stderr:raise RuntimeError('nn::oe performance configuration import signature was refused by the boundary')
# nv::SetGraphicsAllocator and nv::SetGraphicsDevtoolsAllocator (main NSO
# import indexes 350 and 351) both take four arguments - an allocation
# callback, a free callback, a realloc callback and a userdata pointer - and
# return nothing. The fixture drives both through the real guest import path
# with its own callbacks, so the boundary has to accept both four-argument
# void signatures or the guest never runs.
nv_assembly=out/'nv-graphics-allocator-fixture.S'
nv_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x17, [x16]
 adr x0, cb_alloc
 adr x1, cb_free
 adr x2, cb_realloc
 mov x3, xzr
 blr x17
 adrp x16, fixture_got2
 add x16, x16, :lo12:fixture_got2
 ldr x17, [x16]
 adr x0, cb_alloc
 adr x1, cb_free
 adr x2, cb_realloc
 mov x3, xzr
 blr x17
 mov x9, #1
 str x9, [x19]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0xc0
.global cb_alloc
cb_alloc: ret
.global cb_free
cb_free: ret
.global cb_realloc
cb_realloc: ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
.section .fixture_got2,"aw"
.global fixture_got2
fixture_got2: .zero 8
''')
run('fixture-nv-graphics-allocator-assemble',['clang',*common,'-c',nv_assembly,'-o',out/'nv-graphics-allocator-fixture.o'])
run('fixture-nv-graphics-allocator-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_got2=0x8208','-e','fixture_entry','--oformat=binary','-o',out/'nv-graphics-allocator-fixture.raw',out/'nv-graphics-allocator-fixture.o'])
raw=(out/'nv-graphics-allocator-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_\0_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_\0'
data[0x1160:0x1160+len(names)]=names
# Three dynsym entries here, so the base RELA table (DT_RELASZ = 72, three
# entries) stays intact: dynamic index 5 is DT_RELASZ, not DT_RELAENT, and
# truncating it would drop the relocation that fills the second GOT slot.
struct.pack_into('<II',data,0x1080,1,3);struct.pack_into('<I',data,0x1130,names.index(b'_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_'))
struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
struct.pack_into('<QQq',data,0x1258,0x8208,(2<<32)|0x101,0)
(out/'nv-graphics-allocator.nso').write_bytes(data)
manifest['generated_fixtures']['nv-graphics-allocator.nso']={'bytes':len(data),'sha256':digest(out/'nv-graphics-allocator.nso'),'kind':'self-authored two-import four-argument nv graphics allocator registration fixture','assembly_sha256':digest(nv_assembly)}
run('host-nv-graphics-allocator-contract',[out/'guest_process_contract.host','--nv-graphics-allocator',out/'nv-graphics-allocator.nso'])
nv_stderr=(out/'host-nv-graphics-allocator-contract'/'stderr.log').read_text()
if '[hle] nv::SetGraphicsAllocator:' not in nv_stderr:raise RuntimeError('nv::SetGraphicsAllocator stub did not run')
if '[hle] nv::SetGraphicsDevtoolsAllocator:' not in nv_stderr:raise RuntimeError('nv::SetGraphicsDevtoolsAllocator stub did not run')
if '[boundary-unsupported]' in nv_stderr:raise RuntimeError('nv graphics allocator import signature was refused by the boundary')
locale_assembly=out/'locale-fixture.S'
locale_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov x0, x1
 mov x1, x2
 mov x2, x3
 mov x3, x4
 mov x18, #0x55
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 sxtw x0, w0
 str x0, [x19]
 str x18, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-locale-assemble',['clang',*common,'-c',locale_assembly,'-o',out/'locale-fixture.o'])
run('fixture-locale-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'locale-fixture.raw',out/'locale-fixture.o'])
raw=(out/'locale-fixture.raw').read_bytes();data=bytearray((out/'resource.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0strncasecmp_l\0';data[0x1160:0x1160+len(names)]=names;struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
(out/'locale.nso').write_bytes(data)
manifest['generated_fixtures']['locale.nso']={'bytes':len(data),'sha256':digest(out/'locale.nso'),'kind':'self-authored typed4 locale integer-return contract','assembly_sha256':digest(locale_assembly)}
run('host-locale-native-unsupported',[out/'guest_process_contract.host','--locale-native',out/'locale.nso'])
# Additional self-authored register-only imports: exact int32 strcmp-family
# result and canonical bool capability, with guest x18 deliberately clobbered.
for basename,symbol,text in [
 ('strcasecmp','strcasecmp',locale_assembly.read_text().replace(' mov x2, x3\n','').replace(' mov x3, x4\n','')),
 ('va-enabled','_ZN2nn2os29IsVirtualAddressMemoryEnabledEv',resource_assembly.read_text().replace(' stp x0, x1, [x19]',' str x0, [x19]').replace(' str x18, [x19, #16]',' str x18, [x19, #8]'))]:
 assembly=out/(basename+'-fixture.S');assembly.write_text(text)
 run('fixture-'+basename+'-assemble',['clang',*common,'-c',assembly,'-o',out/(basename+'-fixture.o')])
 run('fixture-'+basename+'-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/(basename+'-fixture.raw'),out/(basename+'-fixture.o')])
 raw=(out/(basename+'-fixture.raw')).read_bytes();data=bytearray((out/'resource.nso').read_bytes())
 data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
 names=b'\0'+symbol.encode()+b'\0';data[0x1160:0x1160+len(names)]=names;struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
 name=basename+'.nso';(out/name).write_bytes(data)
 manifest['generated_fixtures'][name]={'bytes':len(data),'sha256':digest(out/name),'kind':'self-authored typed native import contract','assembly_sha256':digest(assembly)}
run('host-strcasecmp-native-unsupported',[out/'guest_process_contract.host','--strcasecmp-native',out/'strcasecmp.nso'])
run('host-va-native-unsupported',[out/'guest_process_contract.host','--va-native',out/'va-enabled.nso'])
# Float arguments are loaded by this self-authored guest entry from its own
# input object; they are never encoded as the CLI's six integer arguments.
math_assembly=out/'logf-fixture.S'
math_assembly.write_text(r''' .text
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 str x19, [sp, #16]
 mov x29, sp
 mov x19, x0
 mov x18, #0x55
 ldr s0, [x19]
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16]
 blr x16
 str s0, [x19, #4]
 str x18, [x19, #8]
 mov x0, #0x91
 ldr x19, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 8
''')
run('fixture-logf-assemble',['clang',*common,'-c',math_assembly,'-o',out/'logf-fixture.o'])
run('fixture-logf-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','-e','fixture_entry','--oformat=binary','-o',out/'logf-fixture.raw',out/'logf-fixture.o'])
raw=(out/'logf-fixture.raw').read_bytes();data=bytearray((out/'resource.nso').read_bytes())
data[0x200:0x300]=raw[:0x100].ljust(0x100,b'\0');struct.pack_into('<I',data,0x204,0x4000)
names=b'\0logf\0';data[0x1160:0x1160+len(names)]=names;struct.pack_into('<Q',data,0x2000+3*16+8,len(names))
(out/'logf.nso').write_bytes(data)
manifest['generated_fixtures']['logf.nso']={'bytes':len(data),'sha256':digest(out/'logf.nso'),'kind':'self-authored scalar float s0 input/return contract','assembly_sha256':digest(math_assembly)}
run('host-logf-native-unsupported',[out/'guest_process_contract.host','--logf-native',out/'logf.nso'])
# This self-authored allocator pair has real, distinct module-owned storage and
# calls Query through an admitted import while its consumer is busy. It is not
# SDK/TCB code, host malloc/free, or a claim that ELF TLS is installed.
allocator_assembly=out/'tls-allocator-fixture.S'
allocator_assembly.write_text(r'''.text
.macro import_call offset
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16, #\offset]
 blr x16
.endm
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 str x19, [x20]
 str xzr, [x20, #8]
 mov x18, #0x55
 import_call 8
 str x0, [x19, #168]
 adr x0, fixture_allocate
 adr x1, fixture_free
 import_call 0
 import_call 8
 str x0, [x19, #176]
 str x18, [x19, #144]
 mov x0, #0x91
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0x100
.global fixture_allocate
fixture_allocate:
 stp x29, x30, [sp, #-48]!
 stp x19, x20, [sp, #16]
 stp x21, x22, [sp, #32]
 mov x29, sp
 mov x19, x0
 mov x20, x1
 adrp x21, fixture_state
 add x21, x21, :lo12:fixture_state
 ldr x9, [x21, #8]
 ldr x21, [x21]
 str x18, [x21, #280]
 cbz x9, allocation_seed_ready
 mov x18, #0x66
allocation_seed_ready:
 cmp x9, #2
 b.eq allocation_busy
 ldr x22, [x21]
 add x9, x21, #16
 str x19, [x9, x22, lsl #3]
 add x9, x21, #48
 str x20, [x9, x22, lsl #3]
 add x9, x22, #1
 str x9, [x21]
 add x0, x21, #184
 import_call 16
 str x18, [x21, #152]
 mov x0, #0
 mov x1, #123
 import_call 32
 cmp x19, #4096
 b.eq allocation_null
 adrp x0, fixture_heap
 add x0, x0, :lo12:fixture_heap
 add x0, x0, x22, lsl #8
 cbz x19, allocation_return
 mov w9, #0x5a
 strb w9, [x0]
 sub x10, x19, #1
 strb w9, [x0, x10]
 b allocation_return
allocation_null:
 mov x0, #0
 b allocation_return
allocation_busy:
 mov x0, #16
 import_call 24
allocation_return:
 ldp x21, x22, [sp, #32]
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #48
 ret
.org 0x200
.global fixture_free
fixture_free:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 adrp x19, fixture_state
 add x19, x19, :lo12:fixture_state
 ldr x9, [x19, #8]
 ldr x19, [x19]
 str x18, [x19, #288]
 cbz x9, free_seed_ready
 mov x18, #0x77
free_seed_ready:
 ldr x20, [x19, #8]
 add x9, x19, #80
 str x0, [x9, x20, lsl #3]
 add x9, x19, #112
 str x1, [x9, x20, lsl #3]
 add x20, x20, #1
 str x20, [x19, #8]
 add x0, x19, #184
 import_call 16
 str x18, [x19, #160]
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0x300
.global fixture_host_consumer
fixture_host_consumer:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 str x0, [x20, #8]
 ldr x19, [x20]
 mov x18, #0x41
 mov x0, #48
 import_call 24
 str x0, [x19, #240]
 mov x0, #0
 import_call 24
 str x0, [x19, #248]
 mov x0, #4096
 import_call 24
 str x0, [x19, #256]
 mov x0, #0
 mov x1, #123
 import_call 32
 ldr x0, [x19, #248]
 mov x1, #0
 import_call 32
 ldr x0, [x19, #240]
 mov x1, #48
 import_call 32
 str x18, [x19, #264]
 mov x9, #1
 str x9, [x19, #272]
 mov x0, #0x91
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0x500
.global fixture_uninitialized_consumer
fixture_uninitialized_consumer:
 stp x29, x30, [sp, #-16]!
 mov x29, sp
 mov x0, #48
 import_call 24
 ldp x29, x30, [sp], #16
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 40
.section .fixture_state,"aw"
.global fixture_state
fixture_state: .zero 16
.section .fixture_heap,"aw"
.global fixture_heap
fixture_heap: .zero 1024
''')
run('fixture-tls-allocator-assemble',['clang',*common,'-c',allocator_assembly,'-o',out/'tls-allocator-fixture.o'])
run('fixture-tls-allocator-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_state=0x8280','--section-start=.fixture_heap=0x8400','-e','fixture_entry','--oformat=binary','-o',out/'tls-allocator-fixture.raw',out/'tls-allocator-fixture.o'])
run('fixture-tls-allocator-disassembly',['xcrun','llvm-objdump','--disassemble',out/'tls-allocator-fixture.o'])
raw=(out/'tls-allocator-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
struct.pack_into('<I',data,0x18,0x600);data[0x200:0x800]=raw[:0x600].ljust(0x600,b'\0');struct.pack_into('<I',data,0x204,0x4000)
symbols=['_ZN2nn2os32SetMemoryAllocatorForThreadLocalEPFPvmmEPFvS1_mE','_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv','_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE','nnosAllocateMemoryForThreadLocal','nnosFreeMemoryForThreadLocal'];names=bytearray(b'\0')
for i,name in enumerate(symbols,1):
 offset=len(names);names+=name.encode()+b'\0';struct.pack_into('<IBBHQQ',data,0x1100+i*24,offset,0x12,0,0,0,0)
 struct.pack_into('<QQq',data,0x1300+(i-1)*24,0x8200+(i-1)*8,(i<<32)|0x402,0)
if len(names)>0x140:raise RuntimeError('allocator fixture names overlap relocations')
data[0x11c0:0x11c0+len(names)]=names;struct.pack_into('<II',data,0x1080,1,6)
for i,(tag,value) in enumerate([(4,0x4080),(6,0x4100),(5,0x41c0),(10,len(names)),(7,0x4300),(8,120),(9,24),(0,0)]):struct.pack_into('<QQ',data,0x2000+i*16,tag,value)
(out/'tls-allocator.nso').write_bytes(data)
manifest['generated_fixtures']['tls-allocator.nso']={'bytes':len(data),'sha256':digest(out/'tls-allocator.nso'),'kind':'self-authored pair, IDLE/HOST consumers, distinct storage, zero/null/free and Query/x18 A/B/C','assembly_sha256':digest(allocator_assembly),'abi':{'set_gp_arguments':2,'set_gp_returns':0,'allocate_gp_arguments':2,'allocate_gp_returns':1,'free_gp_arguments':2,'free_gp_returns':0,'fixed_alignment':16}}
run('host-tls-provider-unsupported',[out/'guest_process_contract.host','--tls-provider',out/'tls-allocator.nso'])
run('host-tls-HOST-consumer-unsupported',[out/'guest_process_contract.host','--tls-host-consumer',out/'tls-allocator.nso'])
# Identity fixture: authored register-only pointer/uint64 bridge + zero-value OS cleanup.
identity_assembly=out/'identity-fixture.S'
identity_assembly.write_text(r'''.text
.macro import_call offset
 adrp x16, fixture_got
 add x16, x16, :lo12:fixture_got
 ldr x16, [x16, #\offset]
 blr x16
.endm
.org 0x20
.global fixture_entry
fixture_entry:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 mov x19, x0
 adrp x20, fixture_state
 add x20, x20, :lo12:fixture_state
 str x19, [x20]
 mov x18, #0x55
 import_call 0
 str x0, [x19]
 import_call 0
 str x0, [x19, #8]
 ldr x0, [x19]
 import_call 8
 str x0, [x19, #16]
 ldr x0, [x19, #8]
 import_call 8
 str x0, [x19, #24]
 str x18, [x19, #32]
 add x0, x20, #8
 adr x1, identity_cleanup
 import_call 16
 cbnz w0, identity_error
 mov x0, #0x91
 b identity_return
identity_error:
 mov x0, #0
identity_return:
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.org 0x100
.global identity_cleanup
identity_cleanup:
 stp x29, x30, [sp, #-32]!
 stp x19, x20, [sp, #16]
 mov x29, sp
 adrp x19, fixture_state
 add x19, x19, :lo12:fixture_state
 ldr x19, [x19]
 str x0, [x19, #56]
 ldr x20, [x19, #72]
 add x20, x20, #1
 str x20, [x19, #72]
 import_call 0
 str x0, [x19, #40]
 import_call 8
 str x0, [x19, #48]
 str x18, [x19, #64]
 ldp x19, x20, [sp, #16]
 ldp x29, x30, [sp], #32
 ret
.section .fixture_got,"aw"
.global fixture_got
fixture_got: .zero 24
.section .fixture_state,"aw"
.global fixture_state
fixture_state: .zero 16
''')
run('fixture-identity-assemble',['clang',*common,'-c',identity_assembly,'-o',out/'identity-fixture.o'])
run('fixture-identity-link',['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'--no-undefined','--build-id=none','--image-base=0','-Ttext=0','--section-start=.fixture_got=0x8200','--section-start=.fixture_state=0x8280','-e','fixture_entry','--oformat=binary','-o',out/'identity-fixture.raw',out/'identity-fixture.o'])
run('fixture-identity-disassembly',['xcrun','llvm-objdump','--disassemble',out/'identity-fixture.o'])
raw=(out/'identity-fixture.raw').read_bytes();data=bytearray((out/'contract.nso').read_bytes())
struct.pack_into('<I',data,0x18,0x400);data[0x200:0x600]=raw[:0x400].ljust(0x400,b'\0');struct.pack_into('<I',data,0x204,0x4000)
symbols=['_ZN2nn2os16GetCurrentThreadEv','_ZN2nn2os11GetThreadIdEPKNS0_10ThreadTypeE','_ZN2nn2os15AllocateTlsSlotEPNS0_7TlsSlotEPFvmE'];names=bytearray(b'\0')
for i,name in enumerate(symbols,1):
 offset=len(names);names+=name.encode()+b'\0';struct.pack_into('<IBBHQQ',data,0x1100+i*24,offset,0x12,0,0,0,0)
 struct.pack_into('<QQq',data,0x1300+(i-1)*24,0x8200+(i-1)*8,(i<<32)|0x402,0)
if len(names)>0x140:raise RuntimeError('identity fixture names overlap relocations')
data[0x11c0:0x11c0+len(names)]=names;struct.pack_into('<II',data,0x1080,1,4)
for i,(tag,value) in enumerate([(4,0x4080),(6,0x4100),(5,0x41c0),(10,len(names)),(7,0x4300),(8,72),(9,24),(0,0)]):struct.pack_into('<QQ',data,0x2000+i*16,tag,value)
(out/'identity.nso').write_bytes(data)
manifest['generated_fixtures']['identity.nso']={'bytes':len(data),'sha256':digest(out/'identity.nso'),'kind':'self-authored opaque primary identity + zero-value cleanup fixture','assembly_sha256':digest(identity_assembly),'tid_bits':64}
run('host-identity-native-unsupported',[out/'guest_process_contract.host','--identity-native',out/'identity.nso'])
# End identity fixture.
android=[]
for source in sources:
 obj=out/(source.stem+'.android.o');android.append(obj)
 run('android-compile-'+source.stem,['clang',*common,'-c',source,'-o',obj])
android.append(out/'native_boundary.assembly.android.o')
run('android-compile-boundary-assembly',['clang',*common,'-c',boundary_assembly,'-o',android[-1]])
run('android-compile-contract',['clang',*common,'-c',test,'-o',out/'contract.android.o'])
run('android-compile-identity-recovery-contract',['clang',*common,'-fsanitize=shadow-call-stack','-DNSO_PROFILE_IDENTITY_TESTING=1','-c',test,'-o',out/'identity-recovery-contract.android.o'])
run('android-compile-identity-recovery-provider',['clang',*common,'-fsanitize=shadow-call-stack','-DNSO_PROFILE_IDENTITY_TESTING=1','-c',hle/'guest_process.c','-o',out/'identity-recovery-provider.android.o'])
run('android-compile-tls-allocator-contract',['clang',*common,'-fsanitize=shadow-call-stack','-DNSO_PROFILE_TLS_ALLOCATOR_TESTING=1','-c',test,'-o',out/'tls-allocator-contract.android.o'])
run('android-compile-tls-allocator-provider',['clang',*common,'-fsanitize=shadow-call-stack','-DNSO_PROFILE_TLS_ALLOCATOR_TESTING=1','-c',hle/'guest_process.c','-o',out/'tls-allocator-provider.android.o'])
run('android-compile-argument-recovery-contract',['clang',*common,'-DNSO_PROFILE_ARGUMENT_TESTING=1','-c',test,'-o',out/'argument-recovery-contract.android.o'])
run('android-compile-argument-recovery-provider',['clang',*common,'-DNSO_PROFILE_ARGUMENT_TESTING=1','-c',hle/'guest_process.c','-o',out/'argument-recovery-provider.android.o'])
run('android-compile-resource-recovery-contract',['clang',*common,'-DGUEST_RESOURCE_TESTING=1','-DNSO_OWNED_PAGES_TESTING=1','-c',test,'-o',out/'resource-recovery-contract.android.o'])
run('android-compile-resource-recovery-provider',['clang',*common,'-DGUEST_RESOURCE_TESTING=1','-c',hle/'guest_context.c','-o',out/'resource-recovery-provider.android.o'])
run('android-compile-resource-vm-recovery',['clang',*common,'-DNSO_OWNED_PAGES_TESTING=1','-c',hle/'address_region.c','-o',out/'resource-vm-recovery.android.o'])
run('android-compile-cli',['clang',*common,'-DNSO_WITH_HOST_HLE=1','-DNSO_WITH_GUEST_PROFILE=1','-c',cli,'-o',out/'cli.android.o'])
run('android-compile-boundary-cleanup-contract',['clang',*common,'-DNSO_PROFILE_BOUNDARY_TESTING=1','-c',test,'-o',out/'boundary-cleanup-contract.android.o'])
run('android-compile-boundary-cleanup-provider',['clang',*common,'-Dmprotect=guest_boundary_test_protect','-Dmunmap=guest_boundary_test_unmap','-c',boundary_source,'-o',out/'boundary-cleanup-provider.android.o'])
ld=a.rust_root/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld'
for name,extra in [('guest_process_contract.android',out/'contract.android.o'),('nso_owned_startup.android',out/'cli.android.o'),('guest_process_identity_recovery.android',out/'identity-recovery-contract.android.o'),('guest_process_tls_allocator_recovery.android',out/'tls-allocator-contract.android.o'),('guest_process_boundary_cleanup.android',out/'boundary-cleanup-contract.android.o'),('guest_process_argument_recovery.android',out/'argument-recovery-contract.android.o'),('guest_process_resource_recovery.android',out/'resource-recovery-contract.android.o')]:
 binary=out/name
 objects=android if name!='guest_process_boundary_cleanup.android' else [v for v in android if v.name!='native_boundary.android.o']+[out/'boundary-cleanup-provider.android.o']
 if name=='guest_process_argument_recovery.android':objects=[v for v in android if v.name!='guest_process.android.o']+[out/'argument-recovery-provider.android.o']
 if name=='guest_process_tls_allocator_recovery.android':objects=[v for v in android if v.name!='guest_process.android.o']+[out/'tls-allocator-provider.android.o']
 if name=='guest_process_identity_recovery.android':objects=[v for v in android if v.name!='guest_process.android.o']+[out/'identity-recovery-provider.android.o']
 if name=='guest_process_resource_recovery.android':objects=[v for v in android if v.name not in ['guest_context.android.o','address_region.android.o']]+[out/'resource-recovery-provider.android.o',out/'resource-vm-recovery.android.o']
 run('android-link-'+name,['env',f'DYLD_LIBRARY_PATH={a.rust_root}/lib',ld,'-pie','--dynamic-linker','/system/bin/linker64',
  '--no-undefined','--eh-frame-hdr','-z','max-page-size=16384','-z','relro','-z','now','-z','noexecstack','--build-id=sha1',
  '-Map',out/(name+'.map'),'-o',binary,api/'crtbegin_dynamic.o',*objects,extra,'--start-group',*library_inputs,'--end-group','-L',api,'-lc','-lm','-ldl',api/'crtend_android.o'])
 elf=run('android-inspect-'+name,[tc/'bin/llvm-readelf','--wide','--file-header','--program-headers','--sections','--dynamic','--symbols',binary])
 data=binary.read_bytes()
 if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise RuntimeError('not AArch64 ELF64')
 if 'nso_loader_test_vm' in elf or '.eh_frame_hdr' not in elf or 'GNU_EH_FRAME' not in elf:raise RuntimeError('production/unwind metadata mismatch')
 if name!='guest_process_boundary_cleanup.android' and 'guest_boundary_test_' in elf:raise RuntimeError('boundary test hooks leaked into production')
 if name!='guest_process_argument_recovery.android' and 'guest_process_test_argument_vm' in elf:raise RuntimeError('argument test hooks leaked into production')
 if name!='guest_process_tls_allocator_recovery.android' and 'guest_process_test_tls_allocator' in elf:raise RuntimeError('TLS allocator test hooks leaked into production')
 if name!='guest_process_identity_recovery.android' and 'guest_process_test_identity_vm' in elf:raise RuntimeError('identity test hooks leaked into production')
 if name!='guest_process_resource_recovery.android' and ('guest_context_resource_test_vm' in elf or 'nso_host_owned_pages_test_vm' in elf):raise RuntimeError('resource test hooks leaked into production')
 if not re.search(r'UND\s+strncasecmp(?:\s|$)',elf) or re.search(r'UND\s+strncasecmp_l(?:\s|$)',elf):raise RuntimeError('Android locale bridge must call Bionic non-_l strncasecmp')
 if not re.search(r'UND\s+strcasecmp(?:@|\s|$)',elf):raise RuntimeError('Android strcasecmp bridge must use Bionic API')
 if not re.search(r'UND\s+logf(?:@|\s|$)',elf) or 'Shared library: [libm.so]' not in elf:raise RuntimeError('logf must bind system libm')
 if 'libc++abi.a(cxa_guard.cpp.o)' not in (out/(name+'.map')).read_text():raise RuntimeError('guard not from official compiler runtime')
 manifest.setdefault('android_artifacts',{})[name]={'path':str(binary),'bytes':len(data),'sha256':digest(binary)}
for name,value in manifest['source_sha256'].items():
 if digest(ROOT/name)!=value:raise RuntimeError('source changed during build '+name)
for name,value in manifest['official_members'].items():
 if digest(tc/name)!=value['sha256']:raise RuntimeError('official input changed '+name)
for name,value in manifest['page_owner_inputs'].items():
 if digest(Path(name))!=value:raise RuntimeError('page owner source changed during build '+name)
for name,value in manifest['boundary_inputs'].items():
 if digest(Path(name))!=value:raise RuntimeError('boundary source changed during build '+name)
for name,value in manifest['os_tls_inputs'].items():
 if digest(Path(name))!=value:raise RuntimeError('OS TLS source changed during build '+name)
save();print('Owned profile normal/recovery contracts passed; original post-Query boundary recorded if requested; Android NOT_RUN')
PY
