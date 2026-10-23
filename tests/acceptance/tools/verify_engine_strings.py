#!/usr/bin/env python3
"""Verify the engine-identity evidence for FFRS (T00R).

Read-only. Scans the decompressed ExeFS module images for the symbol and
literal strings that T00's baseline report relies on, and reports a hit count
plus byte offsets for each needle. A missing needle makes the process exit
non-zero, so this can be used as a regression gate.

Modes
-----
  default        : symbol/string needle scan over the module images
  --mode sdk-mw  : only the Metrowerks "SDK MW+" linker module-identification
                   strings and the GNU build-id notes (the engine version and
                   module-role evidence)

Usage
-----
  python3 -I tests/acceptance/tools/verify_engine_strings.py
  python3 -I tests/acceptance/tools/verify_engine_strings.py --mode sdk-mw
  python3 -I tests/acceptance/tools/verify_engine_strings.py \
      --file /path/to/main.bin --json out.json

Exit codes
----------
  0  every needle matched (or, in sdk-mw mode, at least one identification
     string was found)
  1  at least one needle is missing
  2  usage / IO error
"""
import argparse
import json
import mmap
import os
import re
import sys
from pathlib import Path

DEFAULT_LOCAL = Path("/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local")

# (needle id, needle bytes) -- the strings the baseline report depends on.
NEEDLES = [
    ("shipping_binary_path",      b"FFRS-Switch-Shipping.nss"),
    ("project_codename",          b"mucha"),
    ("workspace_path",            b"E:/workspace/mucha"),
    ("src_gameinstance",          b"Source/mucha/GameInstance/CPP_MuchaGameInstance.cpp"),
    ("src_unit_file",             b"CPP_Unit.h"),
    ("src_task_file",             b"CPP_Task.h"),
    ("src_encount_celllist",      b"Encount/Slate/CPP_SW_Encount_CellList.cpp"),
    ("chaos_vd_flags",            b"EChaosVDAABBTreeNodeFlags"),
    ("chaos_vd_flags_isdirty",    b"EChaosVDAABBTreeNodeFlags::IsDirty"),
    ("chaos_vd_runtime",          b"ChaosVDRuntime"),
    ("universal_object_locator",  b"UniversalObjectLocator"),
    ("iris_core",                 b"IrisCore"),
    ("dataflow_engine",           b"DataflowEngine"),
    ("hair_strands_solver",       b"HairStrandsSolver"),
    ("enhanced_input",            b"EnhancedInput"),
    ("enhanced_input_user_settings", b"EnhancedInputUserSettings"),
    ("common_ui",                 b"CommonUI"),
    ("common_loading_screen",     b"CommonLoadingScreen"),
    ("sprite_studio6",            b"SpriteStudio6"),
    ("nvn_device_initialize",     b"nvnDeviceInitialize"),
    ("nvn_queue_initialize",      b"nvnQueueInitialize"),
    ("nvn_commandbuffer_init",    b"nvnCommandBufferInitialize"),
    ("criatom",                   b"CriAtom"),
    ("crimana",                   b"CriMana"),
    ("criware_runtime",           b"CriWareRuntime"),
    ("oodlelz",                   b"OodleLZ"),
    ("oodle_network_handler",     b"OodleNetworkHandlerComponent"),
    ("script_mucha",              b"/Script/mucha"),
    ("script_engine",             b"/Script/Engine"),
    ("script_slate",              b"/Script/Slate"),
    ("script_enhancedinput",      b"/Script/EnhancedInput"),
    ("script_commonui",           b"/Script/CommonUI"),
    ("script_iriscore",           b"/Script/IrisCore"),
]

# Strings that must NOT be present as version evidence for the old claim.
# Kept here so a future re-introduction of "UE 5.4" is caught.
ANTI_NEEDLES = [
    ("ue5_release_tag",   rb"\+\+UE5\+Release-[0-9]"),
    ("ue5_bare",          rb"UE5"),
    ("engine_source_dir", rb"Engine/Source"),
    ("engine_binaries_dir", rb"Engine/Binaries"),
    ("version_5_4",       rb"5\.4(\.[0-9]+)?"),
]

MW_RE = re.compile(rb"SDK MW\+[^\x00]{1,160}")
UE_RE = re.compile(rb"UnrealEngine[-_][0-9][0-9A-Za-z\._\-]*")
MAX_OFFSETS = 12


def scan_needles(path: Path):
    hits, misses = {}, []
    with path.open("rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            for cid, needle in NEEDLES:
                offsets, start, count = [], 0, 0
                while True:
                    p = mm.find(needle, start)
                    if p < 0:
                        break
                    count += 1
                    if len(offsets) < MAX_OFFSETS:
                        offsets.append(p)
                    start = p + 1
                if count:
                    hits[cid] = {"needle": needle.decode("ascii", "replace"),
                                 "count": count, "first_offsets": offsets}
                else:
                    misses.append(cid)
        finally:
            mm.close()
    return hits, misses


def scan_anti(path: Path):
    """Return needles whose *version-literal* interpretation is still plausible."""
    suspicious = []
    with path.open("rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            for cid, pattern in ANTI_NEEDLES:
                for m in re.finditer(pattern, mm):
                    a = max(0, m.start() - 48)
                    b = min(len(mm), m.end() + 48)
                    raw = bytes(mm[a:b])
                    ctx = raw.decode("latin-1").replace("\x00", ".")
                    printable_ratio = sum(
                        1 for c in raw if 0x20 <= c < 0x7F) / max(1, len(raw))
                    suspicious.append({"id": cid, "offset": m.start(),
                                       "value": m.group().decode("latin-1"),
                                       "context": ctx,
                                       "printable_ratio": round(printable_ratio, 3),
                                       "looks_like_version_literal":
                                           printable_ratio >= 0.8})
        finally:
            mm.close()
    return suspicious


def scan_sdk_mw(path: Path):
    mw, gnu = [], []
    with path.open("rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            for m in MW_RE.finditer(mm):
                mw.append({"offset": m.start(),
                           "value": m.group().decode("latin-1")})
            start = 0
            while True:
                i = mm.find(b"GNU\x00", start)
                if i < 0:
                    break
                gnu.append({"offset": i,
                            "build_id": bytes(mm[i + 4:i + 20]).hex()})
                start = i + 1
            ue = [{"offset": m.start(), "value": m.group().decode("latin-1")}
                  for m in UE_RE.finditer(mm)]
        finally:
            mm.close()
    return {"sdk_mw_strings": mw, "gnu_build_id_notes": gnu,
            "unrealengine_literals": ue}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=["needles", "sdk-mw"], default="needles")
    ap.add_argument("--file", action="append", default=None,
                    help="module image to scan; repeatable. Default: all decompressed .bin files.")
    ap.add_argument("--local", default=str(DEFAULT_LOCAL),
                    help="root of the .local directory (default: %(default)s)")
    ap.add_argument("--json", default=None, help="write the JSON result here")
    ap.add_argument("--require-in", default="main.bin",
                    help="only this module image gates the exit code; the others are "
                         "reported for information (sdk.bin/subsdk0.bin are Nintendo "
                         "SDK components and legitimately carry almost no game strings).")
    args = ap.parse_args()

    local = Path(args.local)
    files = [Path(p) for p in args.file] if args.file else sorted(
        (local / "uncompressed").glob("*.bin"))

    if not files:
        sys.stderr.write(
            "no module images found. Run the ExeFS decompression first, or pass --file.\n")
        return 2

    result = {"tool": "verify_engine_strings", "mode": args.mode, "files": {}}
    rc = 0

    for p in files:
        if not p.exists():
            sys.stderr.write("missing file: %s\n" % p)
            return 2
        if args.mode == "sdk-mw":
            data = scan_sdk_mw(p)
            result["files"][str(p)] = data
            if not data["sdk_mw_strings"] and not data["unrealengine_literals"]:
                rc = max(rc, 1)
        else:
            hits, misses = scan_needles(p)
            anti = scan_anti(p)
            gating = p.name == args.require_in
            result["files"][str(p)] = {"hits": hits, "missing": misses,
                                       "gates_exit_code": gating,
                                       "suspicious_version_literals": anti}
            if misses and gating:
                rc = max(rc, 1)
            if gating:
                # A "5.4"-looking byte sequence inside a binary table is not a
                # version string. Only flag it when it really reads as one.
                for s2 in anti:
                    if s2["looks_like_version_literal"]:
                        sys.stderr.write(
                            "WARNING: plausible version literal %r at offset %d in %s\n"
                            % (s2["value"], s2["offset"], p.name))

    if args.json:
        out = Path(args.json)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, indent=2, ensure_ascii=False),
                       encoding="utf-8")
        sys.stderr.write("saved %s\n" % out)
    else:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    return rc


if __name__ == "__main__":
    sys.exit(main())
