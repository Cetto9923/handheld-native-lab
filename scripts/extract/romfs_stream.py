#!/usr/bin/env python3
"""Zero-Disk-Waste Streaming RomFS Parser and Asset Inspector for FF Resonance.

Streams and decrypts RomFS metadata directly from the base NSP using AES-CTR,
without extracting 8.5 GB RomFS to disk.
Inspects UE 5.6.1 asset structure, Pak/IoStore containers, and shader archives.
Outputs lightweight asset summaries to evidence/assets/.
"""
import argparse
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path
from Crypto.Cipher import AES
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

DEFAULT_ROOT = Path("/Users/yuyan9923/GitHub/ff-resonance-rg477m-native")
DEFAULT_NSP = Path("/Users/yuyan9923/Downloads/SwitchGame/[最终幻想：共鸣][FINAL FANTASY RESONANCE]/[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp")


class RomFSStream:
    def __init__(self, nsp_path: Path, keys_path: Path, tik_path: Path, hdr_path: Path):
        self.nsp_path = nsp_path
        self.keys_path = keys_path
        self.tik_path = tik_path
        self.hdr_path = hdr_path
        self._init_crypto()

    def _init_crypto(self):
        # Load keys
        keys = {}
        if not self.keys_path.exists():
            fallback = Path("~/.switch/prod.keys").expanduser()
            if fallback.exists():
                self.keys_path = fallback
        with open(self.keys_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if "=" in line and not line.startswith("#"):
                    k, v = [x.strip() for x in line.split("=", 1)]
                    keys[k] = bytes.fromhex(v)

        self.header_key = keys["header_key"]
        self.titlekek_15 = keys["titlekek_15"]

        # Decrypt TitleKey
        tik_bytes = self.tik_path.read_bytes()
        enc_titlekey = tik_bytes[0x180:0x190]
        cipher = AES.new(self.titlekek_15, AES.MODE_ECB)
        self.titlekey = cipher.decrypt(enc_titlekey)

        # Decrypt Program NCA Header
        hdr_enc = self.hdr_path.read_bytes()
        hdr_dec = bytearray()
        for sector in range(len(hdr_enc) // 0x200):
            tweak = sector.to_bytes(16, "big")
            c = Cipher(algorithms.AES(self.header_key), modes.XTS(tweak))
            d = c.decryptor()
            block = hdr_enc[sector * 0x200 : (sector + 1) * 0x200]
            hdr_dec.extend(d.update(block) + d.finalize())
        self.hdr_dec = bytes(hdr_dec)

        # Section 1 (RomFS) parameters
        media_start_1 = struct.unpack("<I", self.hdr_dec[0x250:0x254])[0]
        self.sec1_offset = media_start_1 * 0x200
        fs_hdr_1 = self.hdr_dec[0x600:0x800]
        self.sec1_ctr = fs_hdr_1[0x140:0x148]

        # RomFS Level 5 data offset within Section 1
        self.lvl5_offset = 0x10d0000
        self.romfs_base_in_nca = self.sec1_offset + self.lvl5_offset

        # Absolute offset of Program NCA in base NSP
        self.nca_abs_nsp_offset = 6480
        self.f_nsp = open(self.nsp_path, "rb")

        # Parse RomFS header
        self._parse_romfs_header()

    def close(self):
        if self.f_nsp:
            self.f_nsp.close()

    def read_romfs_raw(self, offset_in_romfs: int, size: int) -> bytes:
        tot_ofs = self.romfs_base_in_nca + offset_in_romfs
        aligned_ofs = tot_ofs & ~0xF
        lead_in = tot_ofs & 0xF
        tot_len = size + lead_in
        aligned_len = (tot_len + 15) & ~0xF

        self.f_nsp.seek(self.nca_abs_nsp_offset + aligned_ofs)
        enc = self.f_nsp.read(aligned_len)

        ctr = bytearray(16)
        for j in range(8):
            ctr[j] = self.sec1_ctr[7 - j]
        ofs_shift = aligned_ofs >> 4
        for j in range(8):
            ctr[15 - j] = ofs_shift & 0xFF
            ofs_shift >>= 8

        c = Cipher(algorithms.AES(self.titlekey), modes.CTR(bytes(ctr)))
        dec = c.decryptor().update(enc)
        return dec[lead_in : lead_in + size]

    def _parse_romfs_header(self):
        hdr_data = self.read_romfs_raw(0, 0x50)
        (
            self.header_size,
            self.dir_hash_ofs,
            self.dir_hash_sz,
            self.dir_meta_ofs,
            self.dir_meta_sz,
            self.file_hash_ofs,
            self.file_hash_sz,
            self.file_meta_ofs,
            self.file_meta_sz,
            self.data_ofs
        ) = struct.unpack("<QQQQQQQQQQ", hdr_data)

    def parse_file_tree(self):
        dir_meta = self.read_romfs_raw(self.dir_meta_ofs, self.dir_meta_sz)
        file_meta = self.read_romfs_raw(self.file_meta_ofs, self.file_meta_sz)

        # Parse directories
        dirs = {}
        cur = 0
        while cur < len(dir_meta):
            p_dir, s_dir, c_dir, c_file, h_sib, name_len = struct.unpack("<IIIIII", dir_meta[cur : cur + 24])
            name = dir_meta[cur + 24 : cur + 24 + name_len].decode("utf-8")
            dirs[cur] = {
                "name": name,
                "p_dir": p_dir,
                "s_dir": s_dir,
                "c_dir": c_dir,
                "c_file": c_file,
                "offset": cur
            }
            cur += (24 + name_len + 3) & ~3

        # Build full directory paths
        def get_dir_path(offset: int) -> str:
            path_parts = []
            curr = offset
            while curr != 0 and curr in dirs:
                path_parts.append(dirs[curr]["name"])
                curr = dirs[curr]["p_dir"]
            return "/".join(reversed(path_parts))

        for d_off, d_info in dirs.items():
            d_info["full_path"] = get_dir_path(d_off)

        # Parse files
        files = []
        cur = 0
        while cur < len(file_meta):
            p_dir, s_file, d_ofs, d_sz, h_sib, name_len = struct.unpack("<IIQQII", file_meta[cur : cur + 32])
            name = file_meta[cur + 32 : cur + 32 + name_len].decode("utf-8")
            dir_path = dirs[p_dir]["full_path"] if p_dir in dirs else ""
            full_path = f"{dir_path}/{name}" if dir_path else name
            files.append({
                "path": full_path,
                "name": name,
                "dir": dir_path,
                "data_offset": d_ofs,
                "data_size": d_sz,
                "romfs_abs_offset": self.data_ofs + d_ofs
            })
            cur += (32 + name_len + 3) & ~3

        return dirs, files

    def read_file_data(self, d_ofs: int, d_sz: int) -> bytes:
        return self.read_romfs_raw(self.data_ofs + d_ofs, d_sz)


def analyze_manifest_ufs(raw_bytes: bytes):
    lines = raw_bytes.decode("utf-8", "replace").splitlines()
    exts = Counter()
    modules = Counter()
    plugins = set()
    shaders = []
    movies = []

    for line in lines:
        parts = line.split("\t")
        path = parts[0]
        p = Path(path)
        ext = p.suffix.lower()
        exts[ext] += 1

        parts_split = path.split("/")
        top_mod = parts_split[0]
        if len(parts_split) > 1 and parts_split[1] == "Plugins":
            plugins.add(parts_split[2])
            modules[f"Plugin:{parts_split[2]}"] += 1
        elif len(parts_split) > 1:
            modules[f"{top_mod}/{parts_split[1]}"] += 1
        else:
            modules[top_mod] += 1

        low = path.lower()
        if "shader" in low:
            shaders.append(path)
        if ext == ".usm":
            movies.append(path)

    return {
        "total_entries": len(lines),
        "extension_distribution": dict(exts.most_common()),
        "module_distribution": dict(modules.most_common(30)),
        "plugins": sorted(list(plugins)),
        "shader_files": shaders,
        "movie_count": len(movies),
        "movies_sample": movies[:10]
    }


def parse_utoc_header(utoc_bytes: bytes):
    magic = utoc_bytes[:16]
    ver = utoc_bytes[16]
    hdr_sz, entry_cnt, comp_blk_cnt, comp_blk_sz, comp_mth_cnt, comp_mth_len, comp_blk_sz2, dir_idx_sz, part_cnt = struct.unpack("<IIIIIIIII", utoc_bytes[20:56])
    return {
        "magic": magic.decode("latin1", "replace"),
        "version": ver,
        "header_size": hdr_sz,
        "entry_count": entry_cnt,
        "compressed_block_count": comp_blk_cnt,
        "compressed_block_entry_size": comp_blk_sz,
        "compression_method_count": comp_mth_cnt,
        "compression_block_size": comp_blk_sz2,
        "directory_index_size": dir_idx_sz,
        "partition_count": part_cnt
    }


def main():
    parser = argparse.ArgumentParser(description="RomFS streaming asset inspector")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="Project root directory")
    parser.add_argument("--nsp", type=Path, default=DEFAULT_NSP, help="Path to base NSP")
    parser.add_argument("--out-dir", type=Path, default=None, help="Evidence output directory")
    args = parser.parse_args()

    local_dir = args.root / ".local"
    keys_path = local_dir / "keys.txt"
    tik_path = local_dir / "nca/base.tik"
    hdr_path = local_dir / "nca/program_hdr.bin"
    out_dir = args.out_dir or (args.root / "evidence/assets")
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"[+] Initializing RomFS stream from {args.nsp.name}...")
    stream = RomFSStream(args.nsp, keys_path, tik_path, hdr_path)

    print("[+] Parsing RomFS file tree...")
    dirs, files = stream.parse_file_tree()
    print(f"    Directories found: {len(dirs)}")
    print(f"    Files found: {len(files)}")

    files_summary = []
    manifest_ufs_data = None
    manifest_debug_data = None
    manifest_nonufs_data = None
    ue_cmdline_data = None

    for f in files:
        f_rec = {
            "path": f["path"],
            "data_offset": f["data_offset"],
            "size": f["data_size"],
            "size_formatted": f"{f['data_size'] / (1024*1024):.2f} MB" if f["data_size"] > 1024*1024 else f"{f['data_size']} B"
        }
        if f["data_size"] <= 1024 * 1024 and f["data_size"] > 0:
            content = stream.read_file_data(f["data_offset"], f["data_size"])
            f_rec["sha256"] = hashlib.sha256(content).hexdigest()
            if f["name"] == "UECommandLine.txt":
                ue_cmdline_data = content.decode("utf-8", "replace").strip()
            elif f["name"] == "Manifest_DebugFiles_Switch.txt":
                manifest_debug_data = content.decode("utf-8", "replace")
            elif f["name"] == "Manifest_NonUFSFiles_Switch.txt":
                manifest_nonufs_data = content.decode("utf-8", "replace")
        elif f["name"] == "Manifest_UFSFiles_Switch.txt":
            print(f"[+] Streaming & parsing {f['data_size'] / (1024*1024):.2f} MB Manifest_UFSFiles_Switch.txt in memory...")
            content = stream.read_file_data(f["data_offset"], f["data_size"])
            f_rec["sha256"] = hashlib.sha256(content).hexdigest()
            manifest_ufs_data = analyze_manifest_ufs(content)
        elif f["name"] == "FFRS-Switch.utoc":
            utoc_hdr_bytes = stream.read_file_data(f["data_offset"], 256)
            f_rec["iostore_header"] = parse_utoc_header(utoc_hdr_bytes)
        elif f["name"] == "global.utoc":
            utoc_hdr_bytes = stream.read_file_data(f["data_offset"], 256)
            f_rec["iostore_header"] = parse_utoc_header(utoc_hdr_bytes)
        elif f["name"] == "FFRS-Switch.pak":
            # Read FPakInfo at tail
            tail_bytes = stream.read_file_data(f["data_offset"] + f["data_size"] - 512, 512)
            magic_idx = tail_bytes.rfind(bytes.fromhex("e1126f5a"))
            if magic_idx != -1:
                magic, ver = struct.unpack("<II", tail_bytes[magic_idx : magic_idx + 8])
                idx_ofs, idx_sz = struct.unpack("<QQ", tail_bytes[magic_idx + 8 : magic_idx + 24])
                f_rec["pak_info"] = {
                    "magic": f"{magic:#x}",
                    "version": ver,
                    "index_offset": idx_ofs,
                    "index_size": idx_sz
                }

        files_summary.append(f_rec)

    # Compile overall RomFS inventory
    romfs_inventory = {
        "title_id": "0100B8A026F14000",
        "title_name": "FINAL FANTASY RESONANCE",
        "package": args.nsp.name,
        "rights_id": "0100b8a026f140000000000000000016",
        "titlekey_derived": stream.titlekey.hex(),
        "romfs_header": {
            "header_size": stream.header_size,
            "dir_hash_table_offset": stream.dir_hash_ofs,
            "dir_hash_table_size": stream.dir_hash_sz,
            "dir_meta_table_offset": stream.dir_meta_ofs,
            "dir_meta_table_size": stream.dir_meta_sz,
            "file_hash_table_offset": stream.file_hash_ofs,
            "file_hash_table_size": stream.file_hash_sz,
            "file_meta_table_offset": stream.file_meta_ofs,
            "file_meta_table_size": stream.file_meta_sz,
            "data_offset": stream.data_ofs
        },
        "ue_command_line": ue_cmdline_data,
        "files_count": len(files_summary),
        "files": files_summary
    }

    romfs_json_path = out_dir / "romfs_inventory.json"
    print(f"[+] Writing RomFS inventory to {romfs_json_path}...")
    romfs_json_path.write_text(json.dumps(romfs_inventory, indent=2, ensure_ascii=False))

    if manifest_ufs_data:
        manifest_json_path = out_dir / "manifest_ufs_summary.json"
        print(f"[+] Writing Manifest UFS summary to {manifest_json_path}...")
        manifest_json_path.write_text(json.dumps(manifest_ufs_data, indent=2, ensure_ascii=False))

    stream.close()
    print("[SUCCESS] RomFS streaming analysis and inventory generation complete.")


if __name__ == "__main__":
    main()
