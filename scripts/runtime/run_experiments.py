#!/usr/bin/env python3
"""Build and reproduce the real native leaf sample and entry-point boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT=Path(__file__).resolve().parents[2]
MODULE_SHA="6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5"
LEAF_SHA="8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,default=ROOT/".local/uncompressed/main.bin")
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--host-hle",action="store_true",help="enable only byte-evidenced optional host adapters")
    args=parser.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    data=args.input.read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    leaf_digest=hashlib.sha256(data[0x694bd0+256:0x694bd0+256+44]).hexdigest()
    if digest!=MODULE_SHA or leaf_digest!=LEAF_SHA:
        raise ValueError("private input does not match the reviewed native sample")
    del data
    manifest={"input_sha256":digest,"leaf_sha256":leaf_digest,"host":platform.platform(),
              "architecture":platform.machine(),"host_hle":args.host_hle,"cases":{},"commands":[]}
    for key,cmd in [("head",["git","rev-parse","HEAD"]),("dirty",["git","status","--short"]),
                    ("compiler",["clang","--version"])]:
        manifest[key]=subprocess.check_output(cmd,cwd=ROOT,text=True)
    def run(name,command):
        command=list(map(str,command));manifest["commands"].append({"name":name,"command":command})
        (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        p=subprocess.run(["python3",str(ROOT/"scripts/runtime/run_capture.py"),
                          "--out",str(out/name),"--timeout","10","--",*command],cwd=ROOT)
        metadata=json.loads((out/name/"run.json").read_text())
        manifest["cases"][name]=metadata
        return p.returncode
    base=["clang","-g","-O2","-Wall","-Wextra","-Werror",ROOT/"src/runtime/nso/nso_loader.c"]
    for name,source in [("nso_call",ROOT/"src/runtime/nso/nso_call.c"),
                        ("game_leaf_sample",ROOT/"tests/native/game_leaf_sample.c")]:
        extra=["-DNSO_WITH_HOST_HLE",ROOT/"src/runtime/hle/nso_host.c",ROOT/"src/runtime/hle/address_region.c",ROOT/"src/runtime/hle/mutex.c",ROOT/"src/runtime/hle/exit_registry.c"] if args.host_hle and name=="nso_call" else []
        if run("build-"+name,[*base,source,*extra,"-o",out/name,"-ldl","-lpthread"]):
            raise RuntimeError("native build failed; inspect run logs")
    if run("leaf-sample",[out/"game_leaf_sample",args.input]):
        raise RuntimeError("native game leaf did not match its mathematical oracle")
    for name,argv in [("info",["--info"]),("malloc",["--symbol","malloc","4096"]),
                      ("startup",["--symbol","nninitStartup"]),("main",["--symbol","nnMain"]),
                      ("constructors",["--init","--info"])]:
        run(name,[out/"nso_call",args.input,*argv])
        manifest["cases"][name]["classification"]="STRUCTURAL_LOAD" if name=="info" else "PLATFORM_BOUNDARY_EXPERIMENT"
    manifest["input_sha256_after"]=hashlib.sha256(args.input.read_bytes()).hexdigest()
    if manifest["input_sha256_after"]!=digest:raise RuntimeError("input changed during experiment")
    manifest["game_startup_passed"]=False
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(json.dumps({"output":str(out),"native_leaf":"16875 cases, no mismatch",
                      "game_startup_passed":False}))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
