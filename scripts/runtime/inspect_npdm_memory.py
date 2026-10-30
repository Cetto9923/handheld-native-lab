#!/usr/bin/env python3
"""Read NPDM memory-related configuration; never infer a process RAM quota.

Format reference (research, not Nintendo documentation):
https://switchbrew.org/w/index.php?title=NPDM&oldid=15259
Does not verify ACID signatures, authorize loading, or execute the program.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if len(data) < 0x80 or data[:4] != b"META":
        raise ValueError("expected complete META header")

    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]

    def span(offset, size):
        if offset < 0 or size < 0 or offset > len(data) - size:
            raise ValueError("NPDM section outside input")
        return data[offset:offset + size]

    aci_offset, aci_size = u32(0x70), u32(0x74)
    acid_offset, acid_size = u32(0x78), u32(0x7c)
    aci = span(aci_offset, aci_size)
    acid = span(acid_offset, acid_size)
    if len(aci) < 0x40 or aci[:4] != b"ACI0":
        raise ValueError("expected ACI0")
    if len(acid) < 0x238 or acid[0x200:0x204] != b"ACID":
        raise ValueError("expected ACID")
    kac_offset, kac_size = struct.unpack_from("<II", aci, 0x30)
    if kac_size % 4 or kac_offset > len(aci) - kac_size:
        raise ValueError("ACI capability extent invalid")
    capabilities = []
    for offset in range(kac_offset, kac_offset + kac_size, 4):
        value = struct.unpack_from("<I", aci, offset)[0]
        # Type is the count of low one bits before the first zero.
        low_ones = 0
        while low_ones < 32 and (value >> low_ones) & 1:
            low_ones += 1
        item = {"input_offset": aci_offset + offset,
                "raw_u32": value, "low_one_bits": low_ones}
        if low_ones == 13:
            item["program_type"] = (value >> 14) & 7
        capabilities.append(item)
    acid_flags = struct.unpack_from("<I", acid, 0x20C)[0]
    result = {
        "schema": "ffrs.npdm.memory-configuration.v1",
        "scope": "static_configuration_only_no_signature_verification",
        "input": str(path.resolve()), "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "format_reference": "https://switchbrew.org/w/index.php?title=NPDM&oldid=15259",
        "flags0": data[0xC], "is_aarch64": bool(data[0xC] & 1),
        "address_space_value": (data[0xC] >> 1) & 7,
        "main_thread_priority": data[0xE], "main_thread_core": data[0xF],
        "main_thread_stack_bytes": u32(0x1C),
        "system_resource_reservation_bytes": u32(0x14),
        "acid_flags": acid_flags, "acid_memory_region_value": (acid_flags >> 2) & 15,
        "aci_capabilities": capabilities,
        "guest_total_memory_quota_bytes": None,
        "guest_total_memory_quota_status": "NOT_ENCODED_BY_THESE_FIELDS",
        "notes": [
            "Main-thread stack configuration is not evidence of a host-owned stack mapping.",
            "System resources reserve kernel memory-management resources, not all process RAM.",
            "Address-space size and memory-region class do not determine available physical RAM.",
            "No signature validation, device execution, loader or QueryMemoryInfo binding.",
        ],
    }
    if hashlib.sha256(path.read_bytes()).hexdigest() != result["sha256"]:
        raise ValueError("input changed while inspecting")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.resolve() == args.input.resolve():
        parser.error("output must not replace the input NPDM")
    result = inspect(args.input)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    print(encoded, end="")


if __name__ == "__main__":
    main()
