#!/usr/bin/env bash
# Build the existing driver against the production LAS-record transfer adapter.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN="${FFRS_LAS_BUILD_DIR:-${ROOT}/.local/las-transfer/$(date -u +%Y%m%dT%H%M%SZ)}"
TC="${FFRS_LAS_TOOLCHAIN:-${ROOT}/../ffrs-android/.local/minimal-ndk/toolchain}"
RUST_ROOT="${FFRS_LAS_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
python3 - "${ROOT}" "${RUN}" "${TC}" "${RUST_ROOT}" <<'PY'
import hashlib,json,os,pathlib,runpy,shlex,shutil,subprocess,sys,zlib
root,run,tc,rust=map(lambda v:pathlib.Path(v).resolve(),sys.argv[1:])
run.mkdir(parents=True,exist_ok=False);(run/'logs').mkdir();freeze=run/'source_freeze'
production=['src/graphics/vulkan_probe/vulkan_transfer_probe.c',
    'src/graphics/vulkan/buffer_resource.c','src/graphics/vulkan/buffer_resource.h',
    'src/graphics/nvn_las/command_record.h','src/graphics/nvn_las/transfer_backend.c',
    'src/graphics/nvn_las/transfer_backend.h','src/graphics/nvn_las/typed_transfer.c',
    'src/graphics/nvn_las/typed_transfer.h','src/graphics/nvn/include/nvn/nvn.h',
    'src/runtime/nso/native_boundary.h','third_party/lasrecomp/LICENSE','third_party/lasrecomp/UPSTREAM.json']
support=['tests/graphics/test_las_transfer_contract.c','tests/graphics/vulkan_mock_driver.c',
    'scripts/graphics/build_las_transfer_contract.sh','scripts/graphics/validate_vulkan_artifact.py',
    'scripts/graphics/validate_vulkan_result.py']
for name in production+support:
    dest=freeze/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/name,dest)
commands=[]
def step(label,command,expected=0,extra_env=None,timeout=60):
    command=list(map(str,command));env={**os.environ,**(extra_env or {})}
    (run/'logs'/f'{label}.command.json').write_text(json.dumps({'argv':command,'environment_overrides':extra_env or {}},indent=2)+'\n')
    result=subprocess.run(command,cwd=freeze,env=env,capture_output=True,timeout=timeout)
    for kind,data in [('stdout',result.stdout),('stderr',result.stderr)]: (run/'logs'/f'{label}.{kind}').write_bytes(data)
    (run/'logs'/f'{label}.exitcode').write_text(str(result.returncode)+'\n')
    commands.append({'label':label,'exit_code':result.returncode,'expected_exit_code':expected,
        'command_sha256':hashlib.sha256((run/'logs'/f'{label}.command.json').read_bytes()).hexdigest()})
    if result.returncode!=expected:
        sys.stderr.buffer.write(result.stderr);sys.stdout.buffer.write(result.stdout)
        raise RuntimeError((label,result.returncode,expected))
    return result
head=step('git-head',['git','-C',root,'rev-parse','HEAD']).stdout.decode().strip()
status=step('git-status',['git','-C',root,'status','--short']).stdout.decode()
(run/'git-head.txt').write_text(head+'\n');(run/'git-status.txt').write_text(status)
source_records=[{'path':name,'sha256':hashlib.sha256((freeze/name).read_bytes()).hexdigest()} for name in production]
source_sha=hashlib.sha256(json.dumps(source_records,sort_keys=True,separators=(',',':')).encode()).hexdigest()
host=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
      '-fsanitize=address,undefined','-fno-omit-frame-pointer','-isystem',tc/'sysroot/usr/include/vulkan',
      '-iquote',tc/'sysroot/usr/include']
backend=['src/graphics/nvn_las/transfer_backend.c','src/graphics/nvn_las/typed_transfer.c']
test='tests/graphics/test_las_transfer_contract.c';driver='src/graphics/vulkan_probe/vulkan_transfer_probe.c'
owner='src/graphics/vulkan/buffer_resource.c'
definitions=['-DFFRS_VK_SOURCE_SHA256="'+source_sha+'"','-DFFRS_VK_GIT_HEAD="'+head+'"']
step('compiler-version',['clang','--version'])
step('host-contract-compile',host+backend+[test,'-o',run/'host_contract'])
contract=step('host-contract',[run/'host_contract']);assert json.loads(contract.stdout)['status']=='PASS'
step('host-driver-compile',host+definitions+['-DFFRS_LAS_DRIVER_MOCK',driver,owner]+backend+[test,'-o',run/'host_driver'])
step('host-resource-compile',host+['-DFFRS_LAS_DRIVER_MOCK','-DFFRS_VK_BUFFER_RESOURCE_UNIT_TEST',owner,test,'-o',run/'host_resource'])
for label,env in [('coherent',{}),('noncoherent',{'MOCK_NONCOHERENT':'1'})]:
    result=step('resource-'+label,[run/'host_resource'],extra_env=env)
    assert json.loads(result.stdout)['status']=='PASS'
    mock=json.loads(result.stderr);assert mock['live_objects']==mock['mapped_memories']==0
cases=[]
def mock_case(label,env,expected):
    result=step('driver-'+label,[run/'host_driver'],expected=expected,
        extra_env={'ASAN_OPTIONS':'detect_leaks=0',**env},timeout=25)
    report=json.loads(result.stdout);mock=json.loads(result.stderr)
    assert report['exit_code']==expected and report['status']==('PASS' if expected==0 else 'FAIL')
    uncertain=report['cleanup']['strategy']=='process_teardown_inflight_uncertain'
    assert (mock['live_objects']>0 and mock['mapped_memories']>0) if uncertain else (mock['live_objects']==0 and mock['mapped_memories']==0)
    if expected==0:
        runpy.run_path(str(freeze/'scripts/graphics/validate_vulkan_result.py'))['validate'](report,result.returncode,source_sha)
        assert report['las_transfer']['typed_record_path'] and report['las_transfer']['gpu_completion_retired']
        assert report['las_transfer']['backend_copy_count']==1 and report['las_transfer']['backend_copy_bytes']==65536
    cases.append({'case':label,'actual_exit':result.returncode,'expected_exit':expected,'scope':'HOST_TEST_DOUBLE_ONLY'})
    return report
baseline=mock_case('normal',{},0);mock_case('noncoherent',{'MOCK_NONCOHERENT':'1'},0)
for call in range(1,len(baseline['stages'])+1):
    failure=mock_case(f'vkresult-{call:02d}',{'MOCK_FAIL_CALL':str(call)},2)
    assert failure['vk_result_errors']==1 and failure['failure_stage']==baseline['stages'][call-1]['stage']
for label,env,expected in [('timeout',{'MOCK_TIMEOUT':'1'},4),('device-lost',{'MOCK_WAIT_DEVICE_LOST':'1'},2),
    ('buffer-mismatch',{'MOCK_CORRUPT':'buffer'},3),('image-mismatch',{'MOCK_CORRUPT':'image'},3),
    ('no-device',{'MOCK_NO_DEVICE':'1'},5),('no-queue',{'MOCK_NO_QUEUE':'1'},5),('no-memory',{'MOCK_NO_HOST_MEMORY':'1'},5)]:
    mock_case(label,env,expected)
watchdog=step('driver-watchdog',[run/'host_driver'],expected=124,
    extra_env={'ASAN_OPTIONS':'detect_leaks=0','MOCK_HANG':'1'},timeout=25)
assert json.loads(watchdog.stdout)['failure']=='PROCESS_WATCHDOG_TIMEOUT'
cases.append({'case':'watchdog','actual_exit':watchdog.returncode,'expected_exit':124,'scope':'HOST_TEST_DOUBLE_ONLY'})
sysroot=tc/'sysroot';resource=tc/'lib/clang/18';api=sysroot/'usr/lib/aarch64-linux-android/28'
flags=['--target=aarch64-linux-android28','--sysroot='+str(sysroot),'-resource-dir',resource,
    '-isystem',sysroot/'usr/include/aarch64-linux-android','-isystem',sysroot/'usr/include/vulkan',
    '-std=c11','-O2','-g','-fPIE','-ffixed-x18','-fno-omit-frame-pointer','-mbranch-protection=bti',
    '-Wall','-Wextra','-Werror','-Wpedantic']
objects=[];deps=[]
for index,name in enumerate([driver,owner]+backend):
    obj=run/f'android-{index}.o';dep=run/f'android-{index}.d';objects.append(obj);deps.append(dep)
    step(f'android-compile-{index}',['clang']+flags+definitions+['-MD','-MF',dep,'-c',name,'-o',obj])
linker=rust/'lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld'
builtins=resource/'lib/linux/libclang_rt.builtins-aarch64-android.a';artifact=run/'vulkan_transfer_probe'
step('android-link',[linker,'--no-undefined','--fatal-warnings','--build-id=sha1','--eh-frame-hdr',
    '-pie','--dynamic-linker','/system/bin/linker64','-z','max-page-size=16384','-z','noexecstack',
    '-z','relro','-z','now','-o',artifact,api/'crtbegin_dynamic.o']+objects+['--start-group',builtins,
    '-L',api,'-lc','-lvulkan','--end-group',api/'crtend_android.o'],extra_env={'DYLD_LIBRARY_PATH':str(rust/'lib')})
step('android-elf',[tc/'bin/llvm-readelf','--file-header','--program-headers','--dynamic-table',artifact])
step('android-symbols',[tc/'bin/llvm-readelf','--symbols',artifact])
step('android-disassembly',['/Library/Developer/CommandLineTools/usr/bin/llvm-objdump','-d',artifact])
symbols=(run/'logs/android-symbols.stdout').read_text()
assert 'ffrs_las_backend_encode' in symbols and 'ffrs_las_typed_encode' in symbols
assert 'las_legacy_mock_barrier' not in symbols and 'other_thread' not in symbols
members_path=tc.parent/'validated-members.json';known=json.loads(members_path.read_text());assert known['whole_archive_sha1_verified'] is False
members={x['path']:x for x in known['members']};inputs=set()
for dep in deps:
    inputs.update(pathlib.Path(v).resolve() for v in shlex.split(dep.read_text().replace('\\\n',' ').split(':',1)[1]))
inputs.update([api/'crtbegin_dynamic.o',api/'crtend_android.o',api/'libc.so',api/'libvulkan.so',builtins])
input_records=[]
for p in sorted(inputs):
    data=p.read_bytes();r={'path':str(p),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    if p.is_relative_to(tc):
        name=str(p.relative_to(tc));crc=f'{zlib.crc32(data)&0xffffffff:08x}'
        assert name in members and members[name]['crc32']==crc and members[name]['bytes']==len(data),name
        r['crc32_rechecked']=crc
    input_records.append(r)
checks=runpy.run_path(str(freeze/'scripts/graphics/validate_vulkan_artifact.py'))['verify_elf'](artifact)
evidence={'schema':'ffrs.vulkan.build.v1','android_execution':'NOT_RUN','git_head_at_build':head,
    'source_sha256':source_sha,'source_hash_scheme':'canonical_path_sha256_json_v1 (12 listed LAS/driver/interface/provenance files)',
    'sources':source_records,'artifact':{'path':str(artifact),'bytes':artifact.stat().st_size,
    'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest()},'elf_checks':checks,'inputs':input_records,
    'commands':commands,'host_driver_cases':cases,'host_asan_ubsan':True,
    'leak_check':'test-double live/mapped counts; LSan disabled for deliberately quarantined owner cases',
    'normal_artifact_test_hooks':False,'component_scope':'LAS typed CommandBuffer -> CmdRec -> registered buffer -> core Vulkan copy; existing driver owns queue/fence',
    'ndk_manifest':{'path':str(members_path),'sha256':hashlib.sha256(members_path.read_bytes()).hexdigest(),
        'source':known['source'],'whole_archive_sha1_verified':False}}
(run/'build-evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
print(json.dumps({'status':'PASS','host_driver_cases':len(cases),'source_sha256':source_sha,
    'artifact':evidence['artifact'],'android_execution':'NOT_RUN'}))
PY
