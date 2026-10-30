#!/usr/bin/env python3
"""Reproduce bounded current-SDK Mutex ABI/call-site evidence, without execution."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
from inspect_nso import inspect
from run_experiments import MODULE_SHA

ROOT=Path(__file__).resolve().parents[2]
SDK_SHA="9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--main",type=Path,default=ROOT/".local/uncompressed/main.bin")
    parser.add_argument("--sdk",type=Path,default=ROOT/".local/uncompressed/sdk.bin")
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    main_bytes,main_offset,main=inspect(args.main)
    sdk_bytes,sdk_offset,sdk=inspect(args.sdk)
    if main["sha256"]!=MODULE_SHA or sdk["sha256"]!=SDK_SHA:
        raise ValueError("Mutex field semantics were reviewed only for these exact supplied builds")
    wanted={s["index"] for s in main["symbols"] if not s["section"] and
            ("Mutex" in s["name"]) and ("nn2os" in s["name"] or s["name"].startswith("nnos"))}
    imported={s["index"]:s["name"] for s in main["symbols"] if s["index"] in wanted}
    relocations={}
    for offset_tag,size_tag in [(7,8),(23,2)]:
        off=main["dynamic_tags"][str(offset_tag)][0]
        size=main["dynamic_tags"][str(size_tag)][0]
        for k in range(0,size,24):
            target,info,addend=struct.unpack_from("<QQq",main_bytes,main_offset(off+k,24))
            if info>>32 in wanted:relocations[target]=info>>32
    stubs={}
    text=main["segments"][0]
    for pc in range(text[1]+max(0,text[2]-0x4000),text[1]+text[2]-16,4):
        w,w1=struct.unpack_from("<II",main_bytes,main_offset(pc,8))
        if (w&0x9f00001f)!=0x90000010 or (w1&0xffc003ff)!=0xf9400211:continue
        imm=((w>>5)&0x7ffff)<<2|((w>>29)&3)
        if imm&(1<<20):imm-=1<<21
        got=(pc&~4095)+(imm<<12)+((w1>>10)&0xfff)*8
        if got in relocations:stubs[pc]={"symbol_index":relocations[got],"got":got}
    calls=[]
    code=main_bytes[text[0]:text[0]+text[2]]
    for i,(w,) in enumerate(struct.iter_unpack("<I",code[:len(code)//4*4])):
        if (w&0x7c000000)!=0x14000000:continue
        pc=text[1]+i*4;imm=w&0x3ffffff
        if imm&(1<<25):imm-=1<<26
        target=pc+imm*4
        if target in stubs:calls.append({"pc":pc,"target":target,**stubs[target]})
    sdk_names=("_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi",
               "_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE",
               "_ZN2nn2os9LockMutexEPNS0_9MutexTypeE",
               "_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE",
               "_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE",
               "_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE",
               "_ZN2nn2os6detail36InternalCriticalSectionImplByHorizon10InitializeEv")
    selected=[]
    for symbol in sdk["symbols"]:
        if symbol["name"] in sdk_names:
            start=sdk_offset(symbol["address"],symbol["size"])
            selected.append({**symbol,"file_offset":start,
                             "code_sha256":hashlib.sha256(sdk_bytes[start:start+symbol["size"]]).hexdigest()})
    result={"input_sha256":main["sha256"],"sdk_sha256":sdk["sha256"],
            "main_text_scanned":text[2],"main_tls_instruction_counts":main["tls_instruction_counts"],
            "sdk_functions":selected,"imports":imported,"stubs":stubs,
            "direct_call_counts":dict(Counter(c["symbol_index"] for c in calls)),"direct_calls":calls,
            "method":"Decode ADRP/LDR GOT references in the final 16-KiB PLT area; scan direct B/BL targets in full text",
            "limits":["Indirect function-pointer calls are not resolved.",
                      "Main field aliases and all inline accesses are not proved absent.",
                      "Main's current first allocator constructor directly clears the observed fields before InitializeMutex.",
                      "No MRS TPIDRRO_EL0 in main does not prove all ELF TLS accesses are safe or compatible."]}
    (args.out/"mutex-abi.json").write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"sdk_functions":len(selected),"direct_calls":len(calls),
                      "call_counts":result["direct_call_counts"],"main_sha256":main["sha256"]}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
