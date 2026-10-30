#!/usr/bin/env python3
"""Build/run host owned-page contracts and cross-link a small Android contract.

Uses the existing validated partial official NDK only. No download, device/ADB
operation or game code is involved. Android execution is left to the coordinator.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--rust-root", type=Path,
                        default=Path.home()/".rustup/toolchains/stable-aarch64-apple-darwin")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to((ROOT/".local").resolve()):
        parser.error("products must stay in this worktree's .local")
    out.mkdir(parents=True, exist_ok=False)
    toolchain = args.toolchain.resolve()
    sysroot = toolchain/"sysroot"
    resource = toolchain/"lib/clang/18"
    api = sysroot/"usr/lib/aarch64-linux-android/28"
    builtins = resource/"lib/linux/libclang_rt.builtins-aarch64-android.a"
    provenance = json.loads((toolchain.parent/"validated-members.json").read_text())
    if provenance["source"] != "https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip":
        raise ValueError("unexpected toolchain provenance")
    known = {item["path"]: item for item in provenance["members"]}
    official = [api/"crtbegin_dynamic.o", api/"crtend_android.o", api/"libc.so", builtins,
                sysroot/"usr/include/sys/mman.h", sysroot/"usr/include/pthread.h"]
    source = ROOT/"src/runtime/hle/address_region.c"
    test = ROOT/"tests/native/owned_pages_contract.c"
    old_test = ROOT/"tests/native/address_region_contract.c"
    sources = [source, source.with_suffix(".h"), test, old_test, Path(__file__).resolve()]
    manifest = {
        "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "dirty": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
        "compiler": subprocess.check_output(["clang", "--version"], text=True),
        "host": platform.platform(), "cases": {}, "official_members": {},
        "official_source": provenance["source"],
        "whole_archive_sha1_verified": provenance["whole_archive_sha1_verified"],
        "source_sha256": {str(path.relative_to(ROOT)): digest(path) for path in sources},
        "android_executed": False, "game_resolver_bound": False,
        "physical_budget_claimed": False,
    }

    def save():
        (out/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")

    for path in official:
        name = str(path.relative_to(toolchain))
        data = path.read_bytes()
        crc = f"{zlib.crc32(data):08x}"
        if len(data) != known[name]["bytes"] or crc != known[name]["crc32"]:
            raise ValueError("official member CRC/size mismatch: "+name)
        manifest["official_members"][name] = {"size": len(data), "crc32": crc,
                                               "sha256": hashlib.sha256(data).hexdigest()}
    save()

    def run(name, command):
        save()
        process = subprocess.run(["python3", str(ROOT/"scripts/runtime/run_capture.py"),
                                  "--out", str(out/name), "--timeout", "30", "--",
                                  *map(str, command)], cwd=ROOT, capture_output=True, text=True)
        print(process.stdout.strip())
        record = json.loads((out/name/"run.json").read_text())
        manifest["cases"][name] = record
        save()
        if process.returncode or record["timed_out"]:
            print((out/name/"stderr.log").read_text())
            raise RuntimeError(name+" failed")
        return (out/name/"stdout.log").read_text()

    strict = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
    for name, flags in [("normal", []), ("recovery", ["-DNSO_OWNED_PAGES_TESTING=1"])]:
        binary = out/("owned_pages_"+name+".host")
        run("host-build-"+name, ["clang", *strict, *flags, source, test, "-lpthread", "-o", binary])
        log = run("host-run-"+name, [binary])
        contract = json.loads(log.splitlines()[-1])
        if contract["errors"] or contract["mapped_guest_bytes"] or not contract["ledger_valid"]:
            raise RuntimeError("nonempty/invalid final host ledger")
        manifest.setdefault("host_contracts", {})[name] = contract
    if platform.system() == "Darwin":
        run("host-build-old-region", ["clang", *strict, source, old_test, "-lpthread",
                                       "-o", out/"address_region.host"])
        run("host-run-old-region", [out/"address_region.host"])

    common = ["--target=aarch64-linux-android28", "--sysroot", sysroot, "-resource-dir", resource,
              "-isystem", sysroot/"usr/include/aarch64-linux-android", "-fPIC", *strict]
    objects = []
    for name, path in [("address_region", source), ("contract", test)]:
        obj = out/(name+".android.o")
        objects.append(obj)
        run("android-compile-"+name, ["clang", *common, "-c", path, "-o", obj])
    linker = args.rust_root/"lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
    binary = out/"owned_pages_contract.android"
    run("android-link", ["env", f"DYLD_LIBRARY_PATH={args.rust_root}/lib", linker,
                         "-pie", "--dynamic-linker", "/system/bin/linker64", "--no-undefined",
                         "--eh-frame-hdr", "-z", "max-page-size=16384", "-z", "relro",
                         "-z", "now", "-z", "noexecstack", "--build-id=sha1", "-Map", out/"android.map",
                         "-o", binary, api/"crtbegin_dynamic.o", *objects, builtins,
                         "-L", api, "-lc", api/"crtend_android.o"])
    readelf = run("android-inspect", [toolchain/"bin/llvm-readelf", "--wide", "--file-header",
                                      "--program-headers", "--dynamic", "--symbols", binary])
    data = binary.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
        raise RuntimeError("contract is not AArch64 ELF64")
    if "nso_host_owned_pages_test_vm" in readelf:
        raise RuntimeError("test failure hooks leaked into production Android contract")
    manifest["android_artifact"] = {"path": str(binary), "size": len(data), "sha256": digest(binary),
                                      "test_failure_hooks": False}
    for name, record in manifest["official_members"].items():
        if digest(toolchain/name) != record["sha256"]:
            raise RuntimeError("official input changed: "+name)
    for name, value in manifest["source_sha256"].items():
        if digest(ROOT/name) != value:
            raise RuntimeError("source changed while compiling: "+name)
    save()
    print("host owned-page contracts passed; Android small contract cross-linked, execution NOT_RUN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
