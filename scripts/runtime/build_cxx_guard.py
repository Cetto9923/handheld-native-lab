#!/usr/bin/env python3
"""Offline compiler guard runtime proof and Android CLI/test cross-link prototype.

Reads existing CRC-verified NDK members. Never downloads or executes Android.
Android device scripts/CMake remain owned by their coordinator/worker.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import zlib

ROOT=Path(__file__).resolve().parents[2]


def archive_member(blob,wanted):
    if blob[:8]!=b"!<arch>\n":raise ValueError("not an ar archive")
    p=8;names=b""
    while p<len(blob):
        header=blob[p:p+60]
        if len(header)!=60 or header[58:]!=b"`\n":raise ValueError("invalid ar header")
        size=int(header[48:58]);name=header[:16].decode().strip();body=blob[p+60:p+60+size]
        if len(body)!=size:raise ValueError("truncated ar member")
        if name=="//":names=body
        elif name.startswith("#1/"):
            n=int(name[3:]);name=body[:n].rstrip(b"\0").decode();body=body[n:]
        elif name.startswith("/") and name[1:].isdigit():
            start=int(name[1:]);name=names[start:names.index(b"\n",start)].decode().rstrip("/")
        else:name=name.rstrip("/")
        if name==wanted:return body
        p+=60+size+(size&1)
    raise ValueError(f"archive member absent: {wanted}")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain",type=Path,required=True,help="existing partial official NDK toolchain root")
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--main",type=Path,help="optional reviewed private main for bounded host entry checks")
    parser.add_argument("--rust-root",type=Path,default=Path.home()/".rustup/toolchains/stable-aarch64-apple-darwin")
    args=parser.parse_args();out=args.out.resolve();toolchain=args.toolchain.resolve()
    if not out.is_relative_to(ROOT/".local"):raise ValueError("products must remain in this worktree's .local")
    out.mkdir(parents=True,exist_ok=False)
    sysroot=toolchain/"sysroot";resource=toolchain/"lib/clang/18"
    target_libs=sysroot/"usr/lib/aarch64-linux-android";api_libs=target_libs/"28"
    official=json.loads((toolchain.parent/"validated-members.json").read_text())
    if official["source"]!="https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip":
        raise ValueError("unexpected official package provenance")
    members={r["path"]:r for r in official["members"]}
    paths=[sysroot/"usr/include/c++/v1/cxxabi.h",sysroot/"usr/include/c++/v1/__cxxabi_config.h",
           target_libs/"libc++abi.a",target_libs/"libc++_static.a",
           resource/"lib/linux/aarch64/libunwind.a",resource/"lib/linux/libclang_rt.builtins-aarch64-android.a"]
    manifest={"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        "dirty":subprocess.check_output(["git","status","--short"],cwd=ROOT,text=True),
        "compiler":subprocess.check_output(["clang++","--version"],text=True),"host":platform.platform(),
        "official_source":official["source"],"whole_archive_sha1_verified":official["whole_archive_sha1_verified"],
        "members":{},"cases":{},"android_executed":False,"game_startup_passed":False}
    for path in paths:
        data=path.read_bytes();name=str(path.relative_to(toolchain));record=members[name]
        crc=f"{zlib.crc32(data):08x}"
        if len(data)!=record["bytes"] or crc!=record["crc32"]:raise ValueError(f"member CRC/size mismatch: {name}")
        manifest["members"][name]={"size":len(data),"crc32":crc,"sha256":hashlib.sha256(data).hexdigest()}
    member=archive_member((target_libs/"libc++abi.a").read_bytes(),"cxa_guard.cpp.o")
    (out/"official-cxa_guard.o").write_bytes(member)
    manifest["official_guard_object"]={"size":len(member),"sha256":hashlib.sha256(member).hexdigest()}
    def run(name,command,expected=0):
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        process=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),"--out",str(out/name),
                                "--timeout","30","--",*map(str,command)],cwd=ROOT,capture_output=True,text=True)
        print(process.stdout.strip());manifest["cases"][name]=json.loads((out/name/"run.json").read_text())
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        if process.returncode!=expected or manifest["cases"][name]["timed_out"]:
            print((out/name/"stderr.log").read_text());raise RuntimeError(f"{name} failed")
        return (out/name/"stdout.log").read_text()
    run("official-guard-symbols",["xcrun","llvm-nm","--defined-only",out/"official-cxa_guard.o"])
    run("official-guard-disassembly",["xcrun","llvm-objdump","--disassemble","--demangle",out/"official-cxa_guard.o"])
    hle=ROOT/"src/runtime/hle";nso=ROOT/"src/runtime/nso"
    c_sources=[hle/name for name in ("nso_host.c","address_region.c","mutex.c","exit_registry.c")]
    manifest["source_sha256"]={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [*c_sources,nso/"nso_call.c",nso/"nso_loader.c",nso/"nso_loader.h",
                     hle/"nso_host.h",hle/"address_region.h",hle/"mutex.h",hle/"exit_registry.h",
                     ROOT/"tests/native/cxx_guard_contract.cpp"]}
    host_objects=[]
    for source in c_sources:
        obj=out/(source.stem+".host.o");host_objects.append(obj)
        run("host-compile-"+source.stem,["clang","-std=c11","-O2","-Wall","-Wextra","-Werror",
                                       "-DNSO_WITH_CXX_GUARDS=1","-c",source,"-o",obj])
    test=ROOT/"tests/native/cxx_guard_contract.cpp"
    run("host-link",["clang++","-std=c++17","-O2","-Wall","-Wextra","-Werror",test,
                      *host_objects,"-lpthread","-o",out/"guard_contract.host"])
    host_log=run("host-contract",[out/"guard_contract.host"])
    manifest["host_contract"]=json.loads(host_log.splitlines()[-1])
    if args.main:
        digest=hashlib.sha256(args.main.read_bytes()).hexdigest()
        if digest!="6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5":
            raise ValueError("private input does not match reviewed main")
        manifest["main_sha256"]=digest
        run("host-link-cli",["clang","-std=c11","-O2","-Wall","-Wextra","-Werror","-DNSO_WITH_HOST_HLE=1",
                             nso/"nso_call.c",nso/"nso_loader.c",*host_objects,"-lc++","-lpthread","-ldl","-o",out/"nso_call.host"])
        for name,arguments in [("main",["--symbol","nnMain"]),("malloc",["--symbol","malloc","4096"])]:
            run("host-"+name,[out/"nso_call.host",args.main,*arguments],133)
            diagnostic=(out/("host-"+name)/"stderr.log").read_text()
            if "name=_ZN2nn2os15QueryMemoryInfoEPNS0_10MemoryInfoE" not in diagnostic:
                raise RuntimeError("typed guards did not retain the named real memory boundary")
        manifest["main_sha256_after"]=hashlib.sha256(args.main.read_bytes()).hexdigest()
        if manifest["main_sha256_after"]!=digest:raise RuntimeError("private input changed")
    common=["--target=aarch64-linux-android28","--sysroot",sysroot,"-resource-dir",resource,
            "-isystem",sysroot/"usr/include/aarch64-linux-android","-fPIC","-O2","-g",
            "-fno-omit-frame-pointer","-Wall","-Wextra","-Werror"]
    objects=[];hle_objects=[]
    for source in [*c_sources,nso/"nso_call.c",nso/"nso_loader.c"]:
        obj=out/(source.stem+".android.o");objects.append(obj)
        if source in c_sources:hle_objects.append(obj)
        run("android-compile-"+source.stem,["clang",*common,"-std=c11","-DNSO_WITH_HOST_HLE=1","-c",source,"-o",obj])
    run("android-compile-contract",["clang++",*common,"-nostdinc++","-isystem",sysroot/"usr/include/c++/v1",
                                     "-std=c++17","-c",test,"-o",out/"contract.android.o"])
    linker=args.rust_root/"lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
    link=["env",f"DYLD_LIBRARY_PATH={args.rust_root}/lib",linker,"-pie","--dynamic-linker","/system/bin/linker64",
          "--no-undefined","--eh-frame-hdr","-z","max-page-size=16384","-z","relro","-z","now","-z","noexecstack","--build-id=sha1"]
    libraries=["--start-group",target_libs/"libc++abi.a",target_libs/"libc++_static.a",
               resource/"lib/linux/aarch64/libunwind.a",resource/"lib/linux/libclang_rt.builtins-aarch64-android.a",
               "--end-group","-L",api_libs,"-lc","-lm","-ldl"]
    for name,inputs in [("nso_runtime_hle_guards",objects),("guard_contract.android",[*hle_objects,out/"contract.android.o"])]:
        run("android-link-"+name,[*link,"-Map",out/(name+".map"),"-o",out/name,
                                  api_libs/"crtbegin_dynamic.o",*inputs,*libraries,api_libs/"crtend_android.o"])
        link_map=(out/(name+".map")).read_text()
        if "libc++abi.a(cxa_guard.cpp.o)" not in link_map:raise RuntimeError("guard was not sourced from the verified archive member")
        readelf=run("android-inspect-"+name,[toolchain/"bin/llvm-readelf","--wide","--file-header",
                     "--program-headers","--sections","--dynamic","--symbols",out/name])
        # Raw LLD does not inherit the compiler driver's EH-header default.
        # Android's linked unwinder needs discoverable executable FDE metadata.
        if "GNU_EH_FRAME" not in readelf or ".eh_frame_hdr" not in readelf:
            raise RuntimeError("Android executable lacks discoverable exception-unwind metadata")
        for symbol in ("__cxa_guard_acquire","__cxa_guard_release","__cxa_guard_abort"):
            definitions=[line for line in readelf.splitlines() if line.split() and line.split()[-1]==symbol and " UND " not in line]
            if not definitions:raise RuntimeError(f"linked guard implementation missing: {symbol}")
        blob=(out/name).read_bytes()
        if blob[:6]!=b"\x7fELF\x02\x01" or struct.unpack_from("<H",blob,18)[0]!=183:raise RuntimeError("not AArch64 ELF64")
        manifest.setdefault("android_artifacts",{})[name]={"size":len(blob),"sha256":hashlib.sha256(blob).hexdigest()}
    run("android-typed-guard-relocations",[toolchain/"bin/llvm-readelf","--wide","--relocations",out/"nso_host.android.o"])
    for name,record in manifest["members"].items():
        if hashlib.sha256((toolchain/name).read_bytes()).hexdigest()!=record["sha256"]:raise RuntimeError("official input changed")
    for name,digest in manifest["source_sha256"].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise RuntimeError("source changed while compiling")
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("real compiler guard contract passed on host; Android CLI and contract cross-linked, execution NOT_RUN")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
