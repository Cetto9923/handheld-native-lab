"""Pure-Python LZ4 block-format decompressor.

The Nintendo Switch NSO container stores its .text/.rodata/.data segments as
raw LZ4 *block* payloads (the format produced by ``lz4_compress_default`` and
consumed by ``LZ4_decompress_safe``).  This module implements the block
decoder only: no frame format, no dictionary, no dependency beyond the
stdlib.

Format reference: LZ4 block specification
https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
"""

from __future__ import annotations


class Lz4Error(ValueError):
    """Raised when the input is not a valid LZ4 block stream."""


def decompress_block(src: bytes, expected_size: int) -> bytes:
    """Decompress an LZ4 block stream into exactly ``expected_size`` bytes."""
    out = bytearray()
    src_len = len(src)
    pos = 0

    while pos < src_len:
        token = src[pos]
        pos += 1

        literal_len = token >> 4
        if literal_len == 0x0F:
            while True:
                if pos >= src_len:
                    raise Lz4Error("truncated LZ4 literal length")
                add = src[pos]
                pos += 1
                literal_len += add
                if add != 0xFF:
                    break
        if pos + literal_len > src_len:
            raise Lz4Error("truncated LZ4 literal payload")
        if literal_len:
            out += src[pos:pos + literal_len]
            pos += literal_len

        if pos >= src_len:
            break  # final sequence carries no match part

        if pos + 2 > src_len:
            raise Lz4Error("truncated LZ4 match offset")
        offset = src[pos] | (src[pos + 1] << 8)
        pos += 2
        if offset == 0:
            raise Lz4Error("invalid LZ4 match offset 0")

        match_len = token & 0x0F
        if match_len == 0x0F:
            while True:
                if pos >= src_len:
                    raise Lz4Error("truncated LZ4 match length")
                add = src[pos]
                pos += 1
                match_len += add
                if add != 0xFF:
                    break
        match_len += 4  # LZ4 minimum match length

        if offset > len(out):
            raise Lz4Error("LZ4 match offset points before start of output")
        start = len(out) - offset
        if offset >= match_len:
            out += out[start:start + match_len]
        else:
            # Overlapping match: bytes must be copied one at a time.
            for i in range(match_len):
                out.append(out[start + i])

        if len(out) > expected_size:
            raise Lz4Error(
                f"LZ4 output exceeded expected size ({len(out)} > {expected_size})"
            )

    if len(out) != expected_size:
        raise Lz4Error(
            f"LZ4 output size mismatch (got {len(out)}, expected {expected_size})"
        )
    return bytes(out)
