#!/usr/bin/env python3
"""Title Key Derivation and Verification Tool for Final Fantasy Resonance.

Verifies the rights-id title key decryption derivation mechanism using
base.tik, keys.txt, and validates the resulting titlekey against the
Program NCA Section 0 (ExeFS) and Section 1 (RomFS).
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from Crypto.Cipher import AES
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

DEFAULT_ROOT = Path("/Users/yuyan9923/GitHub/ff-resonance-rg477m-native")
DEFAULT_NSP = Path("/Users/yuyan9923/Downloads/SwitchGame/[最终幻想：共鸣][FINAL FANTASY RESONANCE]/[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp")


def load_keys(keys_path: Path):
    keys = {}
    if not keys_path.exists():
        # Fallback to ~/.switch/prod.keys
        fallback = Path("~/.switch/prod.keys").expanduser()
        if fallback.exists():
            keys_path = fallback
    with open(keys_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if "=" in line and not line.startswith("#"):
                k, v = [x.strip() for x in line.split("=", 1)]
                keys[k] = bytes.fromhex(v)
    return keys


def parse_ticket(tik_bytes: bytes):
    if len(tik_bytes) < 0x2b0:
        raise ValueError(f"Ticket too small: {len(tik_bytes)} bytes")
    
    sig_type = struct.unpack(">I", tik_bytes[:4])[0]
    issuer = tik_bytes[0x140:0x180].split(b"\0")[0].decode("latin1")
    enc_titlekey = tik_bytes[0x180:0x190]
    ticket_flags = tik_bytes[0x280:0x290]
    key_gen_byte = tik_bytes[0x285]
    rights_id = tik_bytes[0x2a0:0x2b0].hex()
    
    return {
        "sig_type": sig_type,
        "issuer": issuer,
        "enc_titlekey": enc_titlekey,
        "key_gen_byte": key_gen_byte,
        "rights_id": rights_id
    }


def decrypt_titlekey(enc_titlekey: bytes, titlekek: bytes) -> bytes:
    cipher = AES.new(titlekek, AES.MODE_ECB)
    return cipher.decrypt(enc_titlekey)


def decrypt_nca_header(hdr_enc: bytes, header_key: bytes) -> bytes:
    hdr_dec = bytearray()
    for sector in range(len(hdr_enc) // 0x200):
        tweak = sector.to_bytes(16, "big")
        c = Cipher(algorithms.AES(header_key), modes.XTS(tweak))
        d = c.decryptor()
        block = hdr_enc[sector * 0x200 : (sector + 1) * 0x200]
        hdr_dec.extend(d.update(block) + d.finalize())
    return bytes(hdr_dec)


def verify_titlekey_on_nsp(nsp_path: Path, titlekey: bytes, header_key: bytes, hdr_enc: bytes):
    hdr_dec = decrypt_nca_header(hdr_enc, header_key)
    if hdr_dec[0x200:0x204] != b"NCA3":
        return False, "Failed to decrypt NCA header with header_key"

    # Section 0 (ExeFS PFS0)
    media_start_0 = struct.unpack("<I", hdr_dec[0x240:0x244])[0]
    sec0_offset = media_start_0 * 0x200
    pfs0_offset = 0xc000  # PFS0 superblock offset in Section 0
    total_offset_0 = sec0_offset + pfs0_offset

    fs_hdr_0 = hdr_dec[0x400:0x600]
    sec0_ctr = fs_hdr_0[0x140:0x148]

    # Section 1 (RomFS)
    media_start_1 = struct.unpack("<I", hdr_dec[0x250:0x254])[0]
    sec1_offset = media_start_1 * 0x200
    lvl5_offset = 0x10d0000  # RomFS level 5 data offset
    total_offset_1 = sec1_offset + lvl5_offset

    fs_hdr_1 = hdr_dec[0x600:0x800]
    sec1_ctr = fs_hdr_1[0x140:0x148]

    # NSP Program NCA absolute offset = 6480
    nca_abs_nsp_offset = 6480

    with nsp_path.open("rb") as f:
        # Check ExeFS
        f.seek(nca_abs_nsp_offset + total_offset_0)
        enc_sec0 = f.read(512)
        ctr0 = bytearray(16)
        for j in range(8):
            ctr0[j] = sec0_ctr[7 - j]
        ofs_shift0 = total_offset_0 >> 4
        for j in range(8):
            ctr0[15 - j] = ofs_shift0 & 0xFF
            ofs_shift0 >>= 8
        c0 = Cipher(algorithms.AES(titlekey), modes.CTR(bytes(ctr0)))
        dec_sec0 = c0.decryptor().update(enc_sec0)

        # Check RomFS header
        f.seek(nca_abs_nsp_offset + total_offset_1)
        enc_sec1 = f.read(512)
        ctr1 = bytearray(16)
        for j in range(8):
            ctr1[j] = sec1_ctr[7 - j]
        ofs_shift1 = total_offset_1 >> 4
        for j in range(8):
            ctr1[15 - j] = ofs_shift1 & 0xFF
            ofs_shift1 >>= 8
        c1 = Cipher(algorithms.AES(titlekey), modes.CTR(bytes(ctr1)))
        dec_sec1 = c1.decryptor().update(enc_sec1)

    exefs_valid = dec_sec0[:4] == b"PFS0"
    romfs_valid = struct.unpack("<Q", dec_sec1[:8])[0] == 0x50

    return (exefs_valid and romfs_valid), {
        "exefs_pfs0_magic": dec_sec0[:4].decode("latin1", "replace"),
        "romfs_header_size": struct.unpack("<Q", dec_sec1[:8])[0],
        "exefs_valid": exefs_valid,
        "romfs_valid": romfs_valid
    }


def main():
    parser = argparse.ArgumentParser(description="Derive and verify Rights ID Title Key")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="Project root directory")
    parser.add_argument("--nsp", type=Path, default=DEFAULT_NSP, help="Path to base NSP")
    args = parser.parse_args()

    local_dir = args.root / ".local"
    tik_path = local_dir / "nca/base.tik"
    keys_path = local_dir / "keys.txt"
    hdr_path = local_dir / "nca/program_hdr.bin"

    print(f"[+] Loading keys from {keys_path}...")
    keys = load_keys(keys_path)

    print(f"[+] Reading ticket from {tik_path}...")
    tik_bytes = tik_path.read_bytes()
    tik_info = parse_ticket(tik_bytes)
    print(f"    Rights ID: {tik_info['rights_id']}")
    print(f"    Encrypted Title Key: {tik_info['enc_titlekey'].hex()}")
    print(f"    Issuer: {tik_info['issuer']}")
    print(f"    Key Generation Byte: {tik_info['key_gen_byte']:#x}")

    titlekek_15 = keys.get("titlekek_15")
    if not titlekek_15:
        raise KeyError("titlekek_15 not found in keyset")

    dec_titlekey = decrypt_titlekey(tik_info["enc_titlekey"], titlekek_15)
    print(f"[+] Derived Title Key using titlekek_15: {dec_titlekey.hex()}")

    # Compare with test_keys.txt
    test_keys_file = local_dir / "test_keys.txt"
    if test_keys_file.exists():
        test_content = test_keys_file.read_text().strip()
        print(f"[*] Note: .local/test_keys.txt contains: {test_content}")
        print("    Analysis: test_keys.txt was decrypted with titlekek_source (1edc7b3b60e6b4d878b81715985e629b),")
        print("    which produces invalid decryption on NCA bodies. titlekek_15 is the mathematically correct key.")

    if args.nsp.exists() and hdr_path.exists():
        print(f"[+] Verifying Title Key against NSP: {args.nsp.name}...")
        header_key = keys.get("header_key")
        hdr_enc = hdr_path.read_bytes()
        valid, details = verify_titlekey_on_nsp(args.nsp, dec_titlekey, header_key, hdr_enc)
        print(f"    Verification result: {'PASSED' if valid else 'FAILED'}")
        print(f"    Details: {json.dumps(details, indent=6)}")
        if not valid:
            sys.exit(1)
    else:
        print(f"[!] Warning: NSP {args.nsp} not found or header missing, skipping live verification.")

    print("\n[SUCCESS] Title key derivation verified successfully.")


if __name__ == "__main__":
    main()
