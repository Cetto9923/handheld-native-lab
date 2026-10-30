#!/usr/bin/env python3
"""Normal isolated module-TLS storage contracts and an optional private source."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/"scripts/runtime"))
from run_experiments import MODULE_SHA
TEMPLATE_SHA="2236311146eecaa733a64cda6108fece94e6693159ba1d3add9bb3f0899834bf"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--main",type=Path,help="optional reviewed uncompressed main; never executed")
    parser.add_argument("--android-sysroot",type=Path,help="optional existing NDK sysroot for an Android-target object")
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/".local"):
        raise ValueError("all generated artifacts must remain in this worktree's .local")
    out.mkdir(parents=True,exist_ok=False)
    manifest={"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
              "dirty":subprocess.check_output(["git","status","--short"],cwd=ROOT,text=True),
              "compiler":subprocess.check_output(["clang","--version"],text=True),
              "host":platform.platform(),"architecture":platform.machine(),
              "cases":{},"game_startup_passed":False,"sdk_tls_bound":False}
    def run(name,command):
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        process=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),"--out",str(out/name),
                                "--timeout","15","--",*map(str,command)],cwd=ROOT,capture_output=True,text=True)
        print(process.stdout.strip());manifest["cases"][name]=json.loads((out/name/"run.json").read_text())
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        if process.returncode or manifest["cases"][name]["timed_out"]:
            print((out/name/"stderr.log").read_text());raise RuntimeError(f"{name} failed")
        return (out/name/"stdout.log").read_text()
    source=ROOT/"src/runtime/hle/module_tls.c"
    flags=["clang","-std=c11","-O2","-Wall","-Wextra","-Werror"]
    run("build-contract",[*flags,source,ROOT/"tests/native/module_tls_contract.c","-lpthread","-o",out/"contract"])
    result=run("contract-run",[out/"contract"])
    manifest["contract"]=json.loads(result.splitlines()[-1])
    if manifest["contract"]["errors"] or manifest["contract"]["payload_value_checks"]!=32968000:
        raise RuntimeError("normal module-TLS contract did not match expected coverage")
    if args.android_sysroot:
        object_path=out/"module_tls.android.o"
        run("compile-android",[*flags,"--target=aarch64-linux-android34",
                              "--sysroot",args.android_sysroot,"-fPIC","-c",source,"-o",object_path])
        object_bytes=object_path.read_bytes()
        if object_bytes[:6]!=b"\x7fELF\x02\x01" or struct.unpack_from("<HH",object_bytes,16)!=(1,183):
            raise RuntimeError("cross object is not little-endian ELF64 AArch64 ET_REL")
        manifest["android_object"]={"path":str(object_path),"size":len(object_bytes),
            "sha256":hashlib.sha256(object_bytes).hexdigest(),"executed":False,
            "classification":"AppleClang Android-target compile with supplied NDK headers; not NDK link or device execution"}
    if args.main:
        data=args.main.read_bytes();digest=hashlib.sha256(data).hexdigest()
        if digest!=MODULE_SHA:raise ValueError("private module does not match reviewed source")
        for i in range(3):
            file,memory,size,_=struct.unpack_from("<IIII",data,0x10+i*16)
            if memory<=0x7767108 and 0x7767108+0x220<=memory+size:
                template=data[file+0x7767108-memory:file+0x7767108-memory+0x220];break
        else:raise ValueError("reviewed initialized template is not file backed")
        if hashlib.sha256(template).hexdigest()!=TEMPLATE_SHA:raise ValueError("template changed")
        manifest.update(main_sha256=digest,template_sha256=TEMPLATE_SHA,template_size=len(template),
                        template_nonzero_bytes=sum(bool(b) for b in template))
        del data
        run("build-private",[*flags,source,ROOT/"src/runtime/nso/nso_loader.c",
                              ROOT/"tests/native/module_tls_from_main.c","-lpthread","-o",out/"from-main"])
        private=out/"private";private.mkdir()
        log=run("mapped-main",[out/"from-main",args.main,private])
        manifest["mapped_main"]=json.loads(log.splitlines()[-1])
        if (private/"loaded-template.bin").read_bytes()!=template:
            raise RuntimeError("relocated mapped template differs from reviewed source")
        expected=b"\0"*16+template+b"\0"*(0x1019-0x220)+b"\0"*7
        if (private/"copied-image.bin").read_bytes()!=expected:
            raise RuntimeError("copied image did not match independent byte oracle")
        manifest["main_sha256_after"]=hashlib.sha256(args.main.read_bytes()).hexdigest()
        if manifest["main_sha256_after"]!=digest:raise RuntimeError("private input changed")
        manifest["private_artifacts"]={path.name:{"size":path.stat().st_size,
                "sha256":hashlib.sha256(path.read_bytes()).hexdigest()} for path in sorted(private.iterdir())}
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("module TLS copy/zero/alignment/lifetime/isolation contracts passed; SDK TLS remains unbound")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
