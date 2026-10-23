#!/usr/bin/env python3
"""Read-only host/PFS0 inventory. Does not decrypt, extract or read key files."""
import datetime
import hashlib
import json
import os
import shutil
import struct
from pathlib import Path

GAME = Path("/Users/yuyan9923/Downloads/SwitchGame/[最终幻想：共鸣][FINAL FANTASY RESONANCE]")


def inventory(path):
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(16)
        if len(header) != 16:
            return {"path": str(path), "error": "short header"}
        magic, count, names_size, _ = struct.unpack("<4sIII", header)
        if magic != b"PFS0" or count > 10000 or names_size > 16 * 1024 * 1024:
            return {"path": str(path), "error": "not bounded PFS0"}
        table_size = 24 * count
        if 16 + table_size + names_size > size:
            return {"path": str(path), "error": "table exceeds file"}
        table = stream.read(table_size)
        names = stream.read(names_size)
    data_base = 16 + table_size + names_size
    entries = []
    for index in range(count):
        offset, length, name_offset, _ = struct.unpack_from("<QQII", table, index * 24)
        end = names.find(b"\0", name_offset) if name_offset < len(names) else -1
        if end < 0 or data_base + offset + length > size:
            return {"path": str(path), "error": "invalid entry bounds"}
        entries.append({"name": names[name_offset:end].decode("utf-8", "replace"),
                        "size": length, "file_offset": data_base + offset})
    return {"path": str(path), "size": size, "outer_format": "PFS0", "entries": entries,
            "pfs0_header_sha256": hashlib.sha256(header + table + names).hexdigest(),
            "digest_scope": "outer header/table/names only; NOT whole-package digest",
            "internal_identity_engine_code_resources": "NOT_VERIFIED"}


def main():
    sdk_roots = {str(Path.home() / "Library/Android/sdk")}
    sdk_roots.update(os.environ[key] for key in ["ANDROID_HOME", "ANDROID_SDK_ROOT"] if os.environ.get(key))
    sdks = []
    for value in sorted(sdk_roots):
        path = Path(value)
        sdks.append({"path": value, "exists": path.is_dir(),
                     "ndk_candidates": sorted(p.name for p in (path / "ndk").glob("*") if p.is_dir())})
    result = {"collected_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "scope": "read-only host PATH/standard SDK paths and PFS0 outer tables; no target execution",
              "tools_found_on_PATH": {name: shutil.which(name) for name in
                                      ["rea", "hactool", "ghidra-headless", "clang++", "cmake", "ninja", "java", "adb"]},
              "sdk_paths": sdks, "packages": [inventory(path) for path in sorted(GAME.glob("*.nsp"))],
              "device": "NOT_CONNECTED_OR_PROBED", "native_game_implementation": "NOT_STARTED"}
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
