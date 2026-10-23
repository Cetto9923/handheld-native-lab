#!/usr/bin/env python3
"""Build byte-identified original leaf + math oracle as Android-target ELF.

Original instructions, generated sources and libraries remain in --out (.local).
This uses existing Apple clang and rust-lld, not the Android NDK toolchain.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import platform
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MODULE_SHA="6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5"
LEAF_SHA="8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9"
REFERENCE="""
/* Handwritten integer mathematics oracle, not decompiled target source. */
__attribute__((visibility("default")))
unsigned ffrs_leaf_reference(int a,int b,int c,unsigned flags) {
    int maximum=a>b?a:b;
    int third=(flags&1)?c:1;
    if(third>maximum)maximum=third;
    unsigned value=(unsigned)maximum|1u,width=0;
    do {width++;value/=2;} while(value);
    return width;
}
"""


def inspect_elf(path):
    raw=path.read_bytes()
    if raw[:6]!=b"\x7fELF\x02\x01":raise ValueError("expected ELF64 little-endian")
    kind,machine=struct.unpack_from("<HH",raw,16)
    shoff=struct.unpack_from("<Q",raw,40)[0]
    shentsize,shnum,shstrndx=struct.unpack_from("<HHH",raw,58)
    if shentsize!=64:raise ValueError("unexpected ELF section size")
    sections=[struct.unpack_from("<IIQQQQIIQQ",raw,shoff+i*64) for i in range(shnum)]
    names=sections[shstrndx]
    namebytes=raw[names[4]:names[4]+names[5]]
    def section_name(s):
        return namebytes[s[0]:namebytes.index(0,s[0])].decode()
    byname={section_name(s):s for s in sections}
    dynstr=byname[".dynstr"];strings=raw[dynstr[4]:dynstr[4]+dynstr[5]]
    symbols={}
    dynsym=byname[".dynsym"]
    for off in range(dynsym[4],dynsym[4]+dynsym[5],24):
        name,info,other,section,address,size=struct.unpack_from("<IBBHQQ",raw,off)
        symbol=strings[name:strings.index(0,name)].decode()
        if section and symbol:
            s=sections[section];start=s[4]+address-s[3];code=raw[start:start+size]
            symbols[symbol]={"address":address,"size":size,"bind":info>>4,"type":info&15,
                             "sha256":hashlib.sha256(code).hexdigest()}
    needed=[]
    dynamic=byname[".dynamic"]
    for off in range(dynamic[4],dynamic[4]+dynamic[5],16):
        tag,value=struct.unpack_from("<QQ",raw,off)
        if tag==1:needed.append(strings[value:strings.index(0,value)].decode())
    original=symbols.get("ffrs_leaf_694bd0",{})
    if kind!=3 or machine!=183 or needed or original.get("sha256")!=LEAF_SHA or original.get("size")!=44:
        raise ValueError("ELF identity, dependencies or original code bytes failed")
    if "ffrs_leaf_reference" not in symbols:raise ValueError("missing mathematical oracle export")
    return {"format":"ELF64 little-endian","type":kind,"machine":machine,
            "needed":needed,"symbols":symbols,"size":len(raw),"sha256":hashlib.sha256(raw).hexdigest()}


def host_compare(library):
    loaded=ctypes.CDLL(str(library))
    native=loaded.ffrs_leaf_694bd0;reference=loaded.ffrs_leaf_reference
    for f in (native,reference):
        f.argtypes=[ctypes.c_int32,ctypes.c_int32,ctypes.c_int32,ctypes.c_uint32]
        f.restype=ctypes.c_uint32
    vals=[-2**31,-128,-1,0,1,2,3,7,8,15,16,255,256,65535,2**31-1]
    flags=[0,1,2,3,2**32-1]
    cases=0
    for a,b,c,f in itertools.product(vals,vals,vals,flags):
        expected=((max(a,b,c if f&1 else 1)&0xffffffff)|1).bit_length()
        got=native(a,b,c,f);oracle=reference(a,b,c,f)
        if got!=expected or oracle!=expected:
            raise ValueError(f"native/math/Python mismatch at {(a,b,c,f)}: {got}/{oracle}/{expected}")
        cases+=1
    return {"platform":"host-macos-arm64","cases":cases,"mismatches":0,"comparands":3}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,default=ROOT/".local/uncompressed/main.bin")
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--lld",type=Path,default=Path.home()/".rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/rust-lld")
    args=parser.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    data=args.input.read_bytes();digest=hashlib.sha256(data).hexdigest()
    leaf=data[0x694bd0+256:0x694bd0+256+44]
    if digest!=MODULE_SHA or hashlib.sha256(leaf).hexdigest()!=LEAF_SHA:
        raise ValueError("input does not match the reviewed function")
    metadata={"input_sha256":digest,"module_offset":"0x694bd0","leaf_sha256":LEAF_SHA,
              "host":platform.platform(),"target":"aarch64-linux-android34",
              "toolchain":"existing Apple clang + rust-lld; not official NDK compiler",
              "android_executed":False,"game_startup_passed":False,"commands":[]}
    (out/"reference.c").write_text(REFERENCE)
    byte_lines="\n".join(".byte "+",".join(str(b) for b in leaf[i:i+16]) for i in range(0,len(leaf),16))
    instruction_lines="\n".join(f".inst 0x{word:08x}" for (word,) in struct.iter_unpack("<I",leaf))
    (out/"leaf.S").write_text(".text\n.p2align 2\n.global ffrs_leaf_694bd0\n.type ffrs_leaf_694bd0,%function\nffrs_leaf_694bd0:\n"+instruction_lines+"\n.size ffrs_leaf_694bd0, .-ffrs_leaf_694bd0\n.section .note.GNU-stack,\"\",%progbits\n")
    def run(name,command):
        command=list(map(str,command));metadata["commands"].append(dict(name=name,command=command))
        (out/"manifest.json").write_text(json.dumps(metadata,indent=2)+"\n")
        p=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),
                          "--out",str(out/name),"--timeout","30","--",*command],cwd=ROOT)
        if p.returncode:raise RuntimeError(f"command {name} failed; see its logs")
    common=["clang","--target=aarch64-linux-android34","-ffreestanding","-fPIC","-O2","-Wall","-Wextra","-Werror"]
    run("compile-leaf",[*common,"-c",out/"leaf.S","-o",out/"leaf.o"])
    run("compile-reference",[*common,"-c",out/"reference.c","-o",out/"reference.o"])
    # The installed rust-lld's rpath misses its own toolchain lib directory.
    # Scope the repair to this child process; do not rewrite the toolchain.
    lld_command=[args.lld]
    if len(args.lld.parents)>3 and (args.lld.parents[3]/"libLLVM.dylib").is_file():
        lld_command=["env",f"DYLD_LIBRARY_PATH={args.lld.parents[3]}",args.lld]
    run("link-android",[*lld_command,"-flavor","gnu","-shared","--no-undefined","--hash-style=both",
                        "-soname","libffrs_leaf.so",out/"leaf.o",out/"reference.o","-o",out/"libffrs_leaf.so"])
    metadata["elf"]=inspect_elf(out/"libffrs_leaf.so")
    run("elf-headers",["xcrun","llvm-objdump","--all-headers",out/"libffrs_leaf.so"])
    run("elf-disassembly",["xcrun","llvm-objdump","--disassemble",out/"libffrs_leaf.so"])
    if platform.system()=="Darwin" and platform.machine()=="arm64":
        (out/"leaf-host.S").write_text(".text\n.p2align 2\n.globl _ffrs_leaf_694bd0\n_ffrs_leaf_694bd0:\n"+byte_lines+"\n")
        run("build-host",["clang","-O2","-Wall","-Wextra","-Werror","-dynamiclib",
                           out/"leaf-host.S",out/"reference.c","-o",out/"libffrs_leaf.dylib"])
        metadata["host_comparison"]=host_compare(out/"libffrs_leaf.dylib")
    else:metadata["host_comparison"]={"status":"NOT_RUN","reason":"macOS arm64 required"}
    metadata["input_sha256_after"]=hashlib.sha256(args.input.read_bytes()).hexdigest()
    if metadata["input_sha256_after"]!=digest:raise RuntimeError("input changed")
    (out/"manifest.json").write_text(json.dumps(metadata,indent=2)+"\n")
    print(json.dumps({k:metadata[k] for k in ("elf","host_comparison","android_executed")}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
