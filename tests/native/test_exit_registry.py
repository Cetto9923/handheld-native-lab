#!/usr/bin/env python3
"""Normal CRT contracts: typed registry, true exit vs immediate exit and unload."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def fixture(path):
    image=bytearray(0x2400)
    def put(off,fmt,*values):struct.pack_into("<"+fmt,image,off,*values)
    image[:4]=b"NSO0"
    for i,s in enumerate([(0x200,0,0x100,0),(0x1000,0x4000,0x400,0),(0x2000,0x8000,0x400,0x400)]):
        put(0x10+i*16,"IIII",*s)
    put(0x204,"I",0x4000)
    image[0x1000:0x1004]=b"MOD0";put(0x1004,"i",0x4000)
    put(0x1080,"IIIIIIII",1,5,1,0,0,0,0,0)
    strings=b"\0";indices=[]
    for name in (b"atexit",b"exit",b"_Exit",b"test_report_callback"):
        indices.append(len(strings));strings+=name+b"\0"
    image[0x11a0:0x11a0+len(strings)]=strings
    for i,n in enumerate(indices,1):put(0x1100+i*24,"IBBHQQ",n,0x12,0,0,0,0)
    tags=[(4,0x4080),(6,0x4100),(5,0x41a0),(10,len(strings)),(7,0x4240),(8,96),(9,24),(0,0)]
    for i,(t,v) in enumerate(tags):put(0x2000+i*16,"QQ",t,v)
    for i in range(4):put(0x1240+i*24,"QQq",0x8200+i*8,((i+1)<<32)|0x402,0)
    def instructions(pc,words):
        for i,w in enumerate(words):put(0x200+pc+i*4,"I",w)
    # Well-formed AArch64 calls: ADR(callback); imported atexit; imported exit.
    for pc,status,got in [(0x20,23,0x208),(0x60,24,0x210)]:
        adr_pc=pc+8;delta=0xa0-adr_pc
        adr=0x10000000|((delta&3)<<29)|(((delta>>2)&0x7ffff)<<5)
        ldr=0xf9400000|((got//8)<<10)|(16<<5)|17
        instructions(pc,[0xa9bf7bfd,0x910003fd,adr,0x90000050,0xf9410211,0xd63f0220,
                         0x52800000|(status<<5),0x90000050,ldr,0xd63f0220])
    instructions(0xa0,[0xa9bf7bfd,0x910003fd,0x90000050,0xf9410e11,
                       0xd63f0220,0xa8c17bfd,0xd65f03c0])
    delta=0xa0-0xd8
    adr=0x10000000|((delta&3)<<29)|(((delta>>2)&0x7ffff)<<5)
    instructions(0xd0,[0xa9bf7bfd,0x910003fd,adr,0x90000050,0xf9410211,
                       0xd63f0220,0xa8c17bfd,0xd65f03c0])
    path.write_bytes(image)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    def run(name,command,expected=0):
        process=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),"--out",str(out/name),
                                "--timeout","15","--",*map(str,command)],cwd=ROOT,capture_output=True,text=True)
        print(process.stdout.strip())
        if process.returncode!=expected:
            print((out/name/"stderr.log").read_text());raise RuntimeError(f"{name} exit {process.returncode}, expected {expected}")
        return (out/name/"stdout.log").read_text()
    hle=ROOT/"src/runtime/hle"
    run("build-contract",["clang","-O2","-Wall","-Wextra","-Werror",hle/"exit_registry.c",
                          ROOT/"tests/native/exit_registry_contract.c","-lpthread","-o",out/"contract"])
    run("contract-run",[out/"contract"])
    normal=run("normal-exit",[out/"contract","--normal-exit"],23)
    immediate=run("immediate-exit",[out/"contract","--immediate-exit"],24)
    if normal.count("guest normal-exit callback executed")!=1 or "guest normal-exit callback executed" in immediate:
        raise RuntimeError("native process exit callback policy failed")
    fixture(out/"exit-contract.nso")
    sources=[ROOT/"src/runtime/nso/nso_loader.c",hle/"nso_host.c",hle/"address_region.c",
             hle/"mutex.c",hle/"exit_registry.c",ROOT/"tests/native/exit_registry_bridge.c"]
    run("build-bridge",["clang","-DNSO_WITH_HOST_HLE","-O2","-Wall","-Wextra","-Werror",
                        *sources,"-lpthread","-ldl","-o",out/"bridge"])
    native_normal=run("native-normal",[out/"bridge",out/"exit-contract.nso","normal"],23)
    native_immediate=run("native-immediate",[out/"bridge",out/"exit-contract.nso","immediate"],24)
    unload=run("native-unload",[out/"bridge",out/"exit-contract.nso","unload"])
    if native_normal.count("native guest callback executed")!=1 or "native guest callback executed" in native_immediate or "native guest callback executed" in unload:
        raise RuntimeError("actual CLI guest exit/unload policy failed")
    (out/"summary.json").write_text(json.dumps({"registry_contract":True,"normal_exit":23,
        "immediate_exit":24,"cli_native_normal_exit":23,"cli_native_immediate_exit":24,
        "module_unload_invoked_callbacks":False},indent=2)+"\n")
    print("typed callback, actual CLI priority, normal/immediate exit and unload contracts passed")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
