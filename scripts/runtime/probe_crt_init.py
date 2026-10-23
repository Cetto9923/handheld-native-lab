#!/usr/bin/env python3
"""Capture this build's real DT_INIT arguments and native SDK early-return gate.

This diagnostic stops before the main initializer resumes. It does not register
the SDK as a compatible CRT, set a TLS allocator, or run SDK constructors.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from inspect_nso import inspect
from run_experiments import MODULE_SHA

ROOT=Path(__file__).resolve().parents[2]
SDK_SHA="9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9"
SLOTS=[0x776f120,0x776f128,0x776f130,0x776f138,0x776f140,0x776f148,0x776f158,
       0x776f160,0x776f170,0x776f168,0x776f118,0x776f150,0x776f110,0x776f108,
       0x776f100,0x776f0f8,0x776f0f0,0x776f0e8,0x776f0e0,0x776f0d8,0x776f0d0]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main",type=Path,default=ROOT/".local/uncompressed/main.bin")
    parser.add_argument("--sdk",type=Path,default=ROOT/".local/uncompressed/sdk.bin")
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    data,fo,main_info=inspect(args.main);sdk_data,sdk_fo,sdk_info=inspect(args.sdk)
    if main_info["sha256"]!=MODULE_SHA or sdk_info["sha256"]!=SDK_SHA:
        raise ValueError("CRT evidence is limited to the reviewed supplied builds")
    tags={int(k):v[0] for k,v in main_info["dynamic_tags"].items()}
    # Positive verification that these original GOT values are all RELR targets.
    wanted=set(SLOTS);found=set();cursor=0
    for p in range(tags[36],tags[36]+tags[35],8):
        entry=struct.unpack_from("<Q",data,fo(p,8))[0]
        if not entry&1:
            if entry in wanted:found.add(entry)
            cursor=entry+8
        else:
            for bit in range(1,64):
                target=cursor+(bit-1)*8
                if entry&(1<<bit) and target in wanted:found.add(target)
            cursor+=63*8
    if found!=wanted:raise ValueError("reviewed DT_INIT GOT relocation coverage changed")
    arguments=[dict(index=i,location=f"x{i}" if i<8 else f"sp+{(i-8)*8}",
                    got=slot,expected_module_offset=struct.unpack_from("<Q",data,fo(slot,8))[0],
                    relocation="DT_RELR") for i,slot in enumerate(SLOTS)]
    def identity(blob,offset_func,offset,size):
        pos=offset_func(offset,size)
        return dict(module_offset=offset,size=size,sha256=hashlib.sha256(blob[pos:pos+size]).hexdigest())
    manifest={"main_sha256":main_info["sha256"],"sdk_sha256":sdk_info["sha256"],
        "head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        "dirty":subprocess.check_output(["git","status","--short"],cwd=ROOT,text=True),
        "compiler":subprocess.check_output(["clang","--version"],text=True),
        "main_dt_init":identity(data,fo,tags[12],0x160),
        "sdk_init_dso":identity(sdk_data,sdk_fo,0x98e30,0x9d8),
        "sdk_allocator_gate":identity(sdk_data,sdk_fo,0x144620,20),
        "sdk_first_dependency":"_ZN2nn2os40MemoryAllocatorForThreadLocalInitializedEv",
        "dso_field_use_inference":{"record_size":0x148,"template_pointer_argument":2,
            "initialized_length":arguments[3]["expected_module_offset"]-arguments[2]["expected_module_offset"],
            "aggregate_length":arguments[7]["expected_module_offset"]-arguments[2]["expected_module_offset"],
            "alignment":max(arguments[5]["expected_module_offset"]-arguments[2]["expected_module_offset"],
                            arguments[9]["expected_module_offset"]-arguments[6]["expected_module_offset"]),
            "source":"SDK +0x98e30 offsets +0x1f4 through +0x218; original source names unavailable"},
        "arguments":arguments,"cases":{},"game_startup_passed":False,
        "limits":["21 GP words establish machine calling convention, not original parameter names/types.",
                  "SDK TLS allocator is intentionally uninitialized; only its observed early-return path executes.",
                  "SDK DT_INIT, constructors, ThreadType/TLS setup and DSO registration do not execute.",
                  "The diagnostic exits 86 before main consumes the SDK result or calls ProtectRelro/constructors."]}
    def run(name,command,expected):
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        process=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),"--out",str(out/name),
                                "--timeout","15","--",*map(str,command)],cwd=ROOT,capture_output=True,text=True)
        print(process.stdout.strip())
        metadata=json.loads((out/name/"run.json").read_text());manifest["cases"][name]=metadata
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        if process.returncode!=expected or metadata["timed_out"]:
            print((out/name/"stderr.log").read_text());raise RuntimeError(f"{name}: expected {expected}")
        return (out/name/"stdout.log").read_text()
    run("build",["clang","-g","-O2","-Wall","-Wextra","-Werror",ROOT/"src/runtime/nso/nso_loader.c",
                 ROOT/"tests/native/crt_init_probe.c","-lpthread","-o",out/"probe"],0)
    for mode in ("capture","sdk-gate"):
        log=run(mode,[out/"probe",args.main,args.sdk,mode],86)
        for arg in arguments:
            expected=f"argument[{arg['index']}] location={arg['location']} got=0x{arg['got']:x} module_offset=0x{arg['expected_module_offset']:x} match=1"
            if expected not in log:raise RuntimeError(f"argument mismatch: {expected}")
        if mode=="sdk-gate" and ("sdk_tls_allocator_initialized=0" not in log or "sdk_init_dso_result=2 expected=2 initialized=0" not in log):
            raise RuntimeError("SDK original early return did not match static gate")
    manifest["main_sha256_after"]=hashlib.sha256(args.main.read_bytes()).hexdigest()
    manifest["sdk_sha256_after"]=hashlib.sha256(args.sdk.read_bytes()).hexdigest()
    if manifest["main_sha256_after"]!=MODULE_SHA or manifest["sdk_sha256_after"]!=SDK_SHA:
        raise RuntimeError("private input changed")
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(json.dumps({"captured_gp_words":21,"sdk_gate":0,"sdk_init_dso_return":2,
                      "diagnostic_exit":86,"game_startup_passed":False}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
