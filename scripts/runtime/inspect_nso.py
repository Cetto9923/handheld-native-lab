#!/usr/bin/env python3
"""Offline NSO dynamic/symbol/TLS evidence; optional exact instruction slices.

Uses supplied local bytes and existing clang/llvm-objdump only. It does not
execute target code, acquire packages, or modify the input.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def inspect(path):
    data=path.read_bytes()
    def u32(p): return struct.unpack_from("<I",data,p)[0]
    if len(data)<256 or data[:4]!=b"NSO0" or u32(12)&7:
        raise ValueError("expected an uncompressed NSO0")
    seg=[struct.unpack_from("<IIII",data,16+i*16) for i in range(3)]
    def file_offset(addr,length=1):
        for f,m,n,_ in seg:
            if m<=addr and length<=n-(addr-m) and f+addr-m+length<=len(data):
                return f+addr-m
        raise ValueError(f"virtual range 0x{addr:x}+0x{length:x} outside file-backed segments")
    mod=seg[0][1]+struct.unpack_from("<i",data,seg[0][0]+4)[0]
    if data[file_offset(mod,28):file_offset(mod,28)+4]!=b"MOD0":
        raise ValueError("invalid MOD0")
    dynamic=mod+struct.unpack_from("<i",data,file_offset(mod)+4)[0]
    tags={}
    for i in range(4096):
        tag,value=struct.unpack_from("<QQ",data,file_offset(dynamic+i*16,16))
        if tag==0:break
        tags.setdefault(tag,[]).append(value)
    else:raise ValueError("unterminated dynamic table")
    def value(tag,default=0): return tags.get(tag,[default])[0]
    count=u32(file_offset(value(4),8)+4) if value(4) else u32(0x9c)//24
    if not value(4) and seg[1][1]+u32(0x98)!=value(6):
        raise ValueError("header/dynamic symbol extent mismatch")
    symbols=[]
    strtab=file_offset(value(5),value(10))
    for i in range(count):
        name,info,other,section,address,size=struct.unpack_from("<IBBHQQ",data,file_offset(value(6)+i*24,24))
        if name>=value(10):raise ValueError("symbol name out of bounds")
        end=data.index(0,strtab+name,strtab+value(10))
        symbols.append(dict(index=i,name=data[strtab+name:end].decode(),info=info,
                            section=section,address=address,size=size))
    tls={"mrs_tpidr_el0":0,"mrs_tpidrro_el0":0,"msr_tpidr_el0":0,"msr_tpidrro_el0":0}
    first={k:[] for k in tls}
    encodings={0xd53bd040:"mrs_tpidr_el0",0xd53bd060:"mrs_tpidrro_el0",
               0xd51bd040:"msr_tpidr_el0",0xd51bd060:"msr_tpidrro_el0"}
    text=data[seg[0][0]:seg[0][0]+seg[0][2]]
    for i,(word,) in enumerate(struct.iter_unpack("<I",text[:len(text)//4*4])):
        key=encodings.get(word&~31)
        if key:
            tls[key]+=1
            if len(first[key])<16:first[key].append(seg[0][1]+i*4)
    result=dict(input=str(path),size=len(data),sha256=hashlib.sha256(data).hexdigest(),
                build_id=data[0x40:0x60].hex(),segments=seg,mod0=mod,dynamic=dynamic,
                dynamic_tags={str(k):v for k,v in tags.items()},symbol_count=count,
                import_count=sum(1 for s in symbols[1:] if not s["section"]),
                symbols=symbols,tls_instruction_counts=tls,tls_first_offsets=first)
    return data,file_offset,result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--slice",action="append",default=[],help="module_offset:length")
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    data,file_offset,result=inspect(args.input)
    result["slices"]=[]
    for value in args.slice:
        off,size=map(lambda v:int(v,0),value.split(":"))
        raw=data[file_offset(off,size):file_offset(off,size)+size]
        stem=args.out/f"slice-{off:x}"
        source=stem.with_suffix(".S")
        # The derived instruction bytes remain in this caller-owned local output.
        source.write_text(".text\n.globl _slice\n_slice:\n"+
                          "\n".join(".byte "+",".join(str(b) for b in raw[i:i+16]) for i in range(0,len(raw),16))+"\n")
        commands=[["clang","-c",str(source),"-o",str(stem.with_suffix(".o"))],
                  ["xcrun","llvm-objdump","--disassemble","--macho",str(stem.with_suffix(".o"))]]
        logs=[]
        for i,command in enumerate(commands):
            process=subprocess.run(command,capture_output=True,text=True)
            stem.with_suffix(f".{i}.stdout.log").write_text(process.stdout)
            stem.with_suffix(f".{i}.stderr.log").write_text(process.stderr)
            logs.append(dict(command=command,returncode=process.returncode))
            if process.returncode:raise RuntimeError(f"static disassembly failed: {process.stderr}")
        result["slices"].append(dict(module_offset=off,file_offset=file_offset(off,size),size=size,
                                    sha256=hashlib.sha256(raw).hexdigest(),commands=logs,
                                    note="disassembly addresses start at zero; add module_offset"))
    (args.out/"inspection.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("size","sha256","build_id","symbol_count","import_count","tls_instruction_counts")}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
