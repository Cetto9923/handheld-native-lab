#!/usr/bin/env python3
"""Positive loader contract fixture: distinct file/virtual offsets, real imports."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def fixture(path):
    image = bytearray(0x2400)
    def put(offset, fmt, *values):
        struct.pack_into("<" + fmt, image, offset, *values)
    image[:4] = b"NSO0"
    for i, segment in enumerate([(0x200,0,0x100,0),(0x1000,0x4000,0x400,0),(0x2000,0x8000,0x400,0x400)]):
        put(0x10+i*16,"IIII",*segment)
    put(0x204,"I",0x4000)
    image[0x1000:0x1004] = b"MOD0"
    put(0x1004,"i",0x4000)
    put(0x1080,"IIIII",1,3,1,0,0)
    names = b"\0host_sum\0missing_platform_call\0"
    image[0x1160:0x1160+len(names)] = names
    put(0x1118,"IBBHQQ",1,0x12,0,0,0,0)
    put(0x1130,"IBBHQQ",10,0x12,0,0,0,0)
    tags = [(4,0x4080),(6,0x4100),(5,0x4160),(10,len(names)),
            (7,0x4240),(8,72),(9,24),(36,0x4300),(35,16),(37,8),(0,0)]
    for i,(tag,value) in enumerate(tags): put(0x2000+i*16,"QQ",tag,value)
    put(0x1240,"QQq",0x8200,(1<<32)|0x101,0)
    put(0x1258,"QQq",0x8208,0x403,0x1234)
    put(0x1270,"QQq",0x8230,(2<<32)|0x402,0)
    put(0x1300,"QQ",0x8210,1|(1<<1)|(1<<3))
    put(0x2210,"QQ",0x100,0x200)
    put(0x2228,"Q",0x300)
    # ADRP x16, data; LDR x16, [x16, GOT offset]; BR x16.
    put(0x220,"III",0x90000050,0xf9410210,0xd61f0200)
    put(0x240,"III",0x90000050,0xf9411a10,0xd61f0200)
    path.write_bytes(image)


def run_capture(out, command):
    result = subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),
                             "--out",str(out),"--timeout","5","--",*map(str,command)],
                            cwd=ROOT,capture_output=True,text=True)
    print(result.stdout.strip())
    return result.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True,exist_ok=False)
    fixture(out/"contract.nso")
    build = ["clang","-O2","-Wall","-Wextra","-Werror","-o",out/"contract",
             ROOT/"src/runtime/nso/nso_loader.c",ROOT/"tests/native/loader_contract.c","-lpthread"]
    if run_capture(out/"build",build):
        return 1
    if run_capture(out/"contract-run",[out/"contract",out/"contract.nso"]):
        print((out/"contract-run/stderr.log").read_text())
        return 1
    code = run_capture(out/"unknown-import",[out/"contract",out/"contract.nso","--unknown"])
    log = (out/"unknown-import/stderr.log").read_text()
    ok = code==133 and "[unresolved-import] index=2 name=missing_platform_call" in log
    (out/"summary.json").write_text(json.dumps({"positive_contract":True,"named_unknown_import":ok},indent=2)+"\n")
    print((out/"contract-run/stdout.log").read_text(),end="")
    print("named_unknown_import =",ok)
    return 0 if ok else 1


if __name__=="__main__":
    raise SystemExit(main())
