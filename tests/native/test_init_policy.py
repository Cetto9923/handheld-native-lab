#!/usr/bin/env python3
"""A valid native initializer wrapper that itself calls its array function."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def fixture(path,with_init):
    image=bytearray(0x2400)
    def put(off,fmt,*values):struct.pack_into("<"+fmt,image,off,*values)
    image[:4]=b"NSO0"
    for i,s in enumerate([(0x200,0,0x100,0),(0x1000,0x4000,0x400,0),(0x2000,0x8000,0x400,0)]):
        put(0x10+i*16,"IIII",*s)
    put(0x204,"I",0x4000)
    image[0x1000:0x1004]=b"MOD0";put(0x1004,"i",0x4000)
    put(0x1080,"IIIII",1,2,1,0,0)
    image[0x11a0:0x11ae]=b"\0report_init\0\0"
    put(0x1118,"IBBHQQ",1,0x12,0,0,0,0)
    tags=[(4,0x4080),(6,0x4100),(5,0x41a0),(10,14),(7,0x4240),(8,48),(9,24),
          (25,0x8200),(27,8)]
    if with_init:tags.append((12,0x20))
    tags.append((0,0))
    for i,(tag,value) in enumerate(tags):put(0x2000+i*16,"QQ",tag,value)
    put(0x1240,"QQq",0x8220,(1<<32)|0x402,0)
    put(0x1258,"QQq",0x8200,0x403,0x80)
    # Synthetic code only. Wrapper reports 1, calls the array function, returns.
    wrapper=[0xa9bf7bfd,0x910003fd,0x52800020,0x90000050,0xf9411211,
             0xd63f0220,0x94000012,0xa8c17bfd,0xd65f03c0]
    array=[0xa9bf7bfd,0x910003fd,0x52800040,0x90000050,0xf9411211,
           0xd63f0220,0xa8c17bfd,0xd65f03c0]
    for pc,words in ((0x20,wrapper),(0x80,array)):
        for i,word in enumerate(words):put(0x200+pc+i*4,"I",word)
    path.write_bytes(image)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    results={}
    def run(name,command,expected=0):
        process=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),"--out",str(out/name),
                                "--timeout","10","--",*map(str,command)],cwd=ROOT,capture_output=True,text=True)
        print(process.stdout.strip());results[name]=json.loads((out/name/"run.json").read_text())
        if process.returncode!=expected or results[name]["timed_out"]:
            print((out/name/"stderr.log").read_text());raise RuntimeError(f"{name}: expected exit {expected}")
    common=["clang","-O2","-Wall","-Wextra","-Werror",ROOT/"src/runtime/nso/nso_loader.c"]
    run("build",[*common,ROOT/"tests/native/init_policy_contract.c","-lpthread","-o",out/"contract"])
    fixture(out/"wrapper.nso",True);fixture(out/"array-only.nso",False)
    for name,source,policy,count in [("wrapper", "wrapper.nso","wrapper",2),
            ("elf","wrapper.nso","elf",3),("legacy","wrapper.nso","legacy",3),
            ("no-init-wrapper","array-only.nso","wrapper",1),("no-init-elf","array-only.nso","elf",1)]:
        run(name,[out/"contract",out/source,policy,str(count)])
    run("build-cli",[*common,ROOT/"src/runtime/nso/nso_call.c","-lpthread","-ldl","-o",out/"cli"])
    run("ambiguous-cli-init",[out/"cli",out/"wrapper.nso","--init","--info"],2)
    (out/"summary.json").write_text(json.dumps(results,indent=2)+"\n")
    print("explicit wrapper/ELF policy, legacy ELF and no-DT_INIT array contracts passed")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
