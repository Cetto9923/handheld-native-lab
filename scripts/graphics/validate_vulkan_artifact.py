#!/usr/bin/env python3
"""Verify ARM64 Android ELF protections and freeze the exact local build inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import zlib


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_elf(path):
    data = Path(path).read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01", "ELF64 little endian required"
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    assert header[1] == 3 and header[2] == 183, "AArch64 ET_DYN required"
    offset, size, count = header[5], header[9], header[10]
    assert size == 56 and count > 0, "invalid program header table"
    entries = [struct.unpack_from("<IIQQQQQQ", data, offset + i * size) for i in range(count)]
    loads = [p for p in entries if p[0] == 1]
    assert loads and all(p[7] >= 16384 for p in loads), "16KiB LOAD alignment required"
    assert all(p[2] % 16384 == p[3] % 16384 for p in loads), "LOAD file/VA alignment mismatch"
    assert all(not (p[1] & 1 and p[1] & 2) for p in loads), "writable executable LOAD"
    stack = [p for p in entries if p[0] == 0x6474E551]
    assert len(stack) == 1 and stack[0][1] & 1 == 0, "non-executable GNU_STACK required"
    assert any(p[0] == 0x6474E552 for p in entries), "GNU_RELRO required"
    assert any(p[0] == 0x6474E550 for p in entries), "GNU_EH_FRAME required"
    interpreter = [p for p in entries if p[0] == 3]
    assert len(interpreter) == 1, "PT_INTERP required"
    p = interpreter[0]
    assert data[p[2]:p[2] + p[5]].rstrip(b"\0") == b"/system/bin/linker64", "wrong Android interpreter"
    dynamic = [p for p in entries if p[0] == 2]
    assert len(dynamic) == 1, "PT_DYNAMIC required"
    p = dynamic[0]
    tags = [struct.unpack_from("<qQ", data, i) for i in range(p[2], p[2] + p[5], 16)]
    assert any(tag == 30 and value & 8 or tag == 0x6FFFFFFB and value & 1 or tag == 24
               for tag, value in tags), "BIND_NOW required"
    return {"elf64": True, "aarch64": True, "pie": True, "load_alignments": [p[7] for p in loads],
            "no_writable_executable_load": True, "noexecstack": True, "relro": True,
            "bind_now": True, "eh_frame_hdr": True, "interpreter": "/system/bin/linker64"}


def main():
    parser = argparse.ArgumentParser()
    for arg in ("build-dir", "toolchain", "members", "compiler", "linker", "builtins"):
        parser.add_argument("--" + arg, required=True)
    args = parser.parse_args()
    run, toolchain = Path(args.build_dir).resolve(), Path(args.toolchain).resolve()
    manifest = json.loads(Path(args.members).read_text())
    assert manifest["whole_archive_sha1_verified"] is False, "expected explicit partial-NDK provenance"
    members = {entry["path"]: entry for entry in manifest["members"]}
    dependency_text = (run / "probe.d").read_text().replace("\\\n", " ")
    dependencies = [Path(p).resolve() for p in shlex.split(dependency_text.split(":", 1)[1])]
    api = toolchain / "sysroot/usr/lib/aarch64-linux-android/28"
    inputs = set(dependencies + [api / name for name in
                  ("crtbegin_dynamic.o", "crtend_android.o", "libc.so", "libvulkan.so")]
                 + [Path(args.builtins).resolve()])
    records = []
    for path in sorted(inputs):
        content = path.read_bytes()
        entry = {"path": str(path), "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
        if path.is_relative_to(toolchain):
            relative = path.relative_to(toolchain).as_posix()
            assert relative in members, f"input absent from validated NDK manifest: {relative}"
            expected = members[relative]
            crc = f"{zlib.crc32(content) & 0xffffffff:08x}"
            assert len(content) == expected["bytes"] and crc == expected["crc32"], f"CRC mismatch: {relative}"
            entry["crc32_rechecked"] = crc
        records.append(entry)
    artifact = run / "vulkan_transfer_probe"
    evidence = {"schema": "ffrs.vulkan.build.v1", "android_execution": "NOT_RUN",
                "git_head_at_build": (run / "git-head.txt").read_text().strip(),
                "source_sha256": digest(run / "source_freeze/src/graphics/vulkan_probe/vulkan_transfer_probe.c"),
                "artifact": {"path": str(artifact), "bytes": artifact.stat().st_size, "sha256": digest(artifact)},
                "elf_checks": verify_elf(artifact), "inputs": records,
                "tools": [{"path": str(Path(p).resolve()), "sha256": digest(Path(p).resolve())}
                          for p in (args.compiler, args.linker)],
                "ndk_manifest": {"path": str(Path(args.members).resolve()), "sha256": digest(args.members),
                                 "source": manifest["source"], "whole_archive_sha1_verified": False}}
    (run / "build-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps({"artifact": evidence["artifact"], "elf_checks": evidence["elf_checks"],
                      "validated_input_count": len(records), "android_execution": "NOT_RUN"}, indent=2))


if __name__ == "__main__":
    main()
