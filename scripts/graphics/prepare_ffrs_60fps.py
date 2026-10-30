#!/usr/bin/env python3
"""Prepare an attributed, guarded derived NSO copy; never modify the source.

CLI production profile is fixed. This does not install, run, upgrade, change a
BuildId, alter clocks, or prove 60FPS. Outputs must be a new directory beneath
this checkout's .local/. If I/O fails after creation, that incomplete directory
is retained for inspection; only a verified copy gets a success manifest.
"""
import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys

REPOSITORY = Path(__file__).resolve().parents[2]
LOCAL_ROOT = REPOSITORY / '.local'
CHUNK_BYTES = 1024 * 1024
SOURCE_URL = ('https://gbatemp.net/threads/final-fantasy-resonance-1-01-tid-'
              '0100606026f12000-bid-84e182f72ee1da4f477804bd05b54c15.685680/')


@dataclass(frozen=True)
class PatchProfile:
    build_id: str
    patch_va: int
    expected_word: bytes
    replacement_word: bytes
    context_va: int
    context_bytes: int
    context_sha256: str


PRODUCTION_PROFILE = PatchProfile(
    build_id='736837afd390442ed7c74a7de730e71b' + '00' * 16,
    patch_va=0x0144B210,
    expected_word=bytes.fromhex('15c59f1a'),
    replacement_word=bytes.fromhex('35008052'),
    context_va=0x0144B1C0,
    context_bytes=160,
    context_sha256='98e9b24d725aa031d9160df450457026b2f8cc208199c469658b1e45f5b1560b',
)


class PreparationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise PreparationError(message)


def sha256_stream(stream):
    stream.seek(0)
    digest = hashlib.sha256()
    while True:
        block = stream.read(CHUNK_BYTES)
        if not block:
            return digest.hexdigest()
        digest.update(block)


def identity(st):
    return st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns


def checked_text_offset(va, size, text, file_bytes):
    file_offset, memory_offset, text_bytes = text
    require(size > 0 and va >= memory_offset and
            size <= text_bytes - (va - memory_offset),
            'requested VA range is outside the NSO text segment')
    offset = file_offset + va - memory_offset
    require(offset >= 0x100 and offset + size <= file_bytes,
            'requested text bytes are outside the input file')
    return offset


def validate(source, profile):
    require(len(bytes.fromhex(profile.build_id)) == 32,
            'profile requires the complete 32-byte BuildId')
    require(len(profile.expected_word) == len(profile.replacement_word) == 4 and
            profile.expected_word != profile.replacement_word and profile.patch_va % 4 == 0,
            'profile must replace exactly one aligned, changed 32-bit word')
    require(profile.context_va <= profile.patch_va and
            profile.patch_va + 4 <= profile.context_va + profile.context_bytes,
            'profile context must contain the complete patch word')
    st = os.fstat(source.fileno())
    require(stat.S_ISREG(st.st_mode), 'input must be a regular file')
    require(st.st_size >= 0x100, 'truncated NSO header')
    source.seek(0)
    header = source.read(0x100)
    require(header[:4] == b'NSO0', 'input must have NSO0 magic')
    flags = struct.unpack_from('<I', header, 0xC)[0]
    require(flags & 7 == 0, 'compressed NSO segments are not supported')
    require(header[0x40:0x60].hex() == profile.build_id.lower(),
            'complete NSO BuildId does not match the fixed current-build profile')
    segments = [struct.unpack_from('<III', header, 0x10 + 0x10 * i) for i in range(3)]
    for file_offset, _, size in segments:
        require(size == 0 or (file_offset >= 0x100 and file_offset + size <= st.st_size),
                'declared NSO segment is outside the input file')
    text = segments[0]
    patch_offset = checked_text_offset(profile.patch_va, 4, text, st.st_size)
    context_offset = checked_text_offset(profile.context_va, profile.context_bytes, text, st.st_size)
    source.seek(patch_offset)
    require(source.read(4) == profile.expected_word, 'original instruction word does not match')
    source.seek(context_offset)
    context = source.read(profile.context_bytes)
    require(hashlib.sha256(context).hexdigest() == profile.context_sha256,
            'instruction context SHA256 does not match')
    original_sha256 = sha256_stream(source)
    require(identity(os.fstat(source.fileno())) == identity(st), 'input changed during validation')
    return st, flags, text, patch_offset, context_offset, original_sha256


def expected_block(block, block_offset, patch_offset, replacement):
    lo, hi = max(block_offset, patch_offset), min(block_offset + len(block), patch_offset + 4)
    if lo >= hi:
        return block
    result = bytearray(block)
    result[lo - block_offset:hi - block_offset] = replacement[lo - patch_offset:hi - patch_offset]
    return bytes(result)


def prepare(source_path, output_directory, *, profile=None):
    """profile is injectable for self-owned fixtures; CLI never accepts one."""
    if profile is None:
        profile = PRODUCTION_PROFILE
    source_path = Path(source_path).resolve(strict=True)
    output_directory = Path(output_directory).resolve()
    require(source_path != output_directory and source_path != output_directory / 'main.nso',
            'source and derived output must be different paths')
    local_root = LOCAL_ROOT.resolve()
    require(output_directory != local_root and local_root in output_directory.parents,
            'output must be a new directory beneath this checkout .local/')
    require(not output_directory.exists(), 'output directory already exists; refusing to overwrite')
    require(output_directory.parent.is_dir(), 'output parent must already exist')
    with source_path.open('rb') as source:
        st, flags, text, patch_offset, context_offset, original_sha256 = validate(source, profile)
        # All identity/word/context gates passed before any derived bytes exist.
        output_directory.mkdir(mode=0o700)
        directory_fd = os.open(output_directory, os.O_RDONLY | os.O_DIRECTORY)
        try:
            fd = os.open('main.nso', os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600,
                         dir_fd=directory_fd)
            with os.fdopen(fd, 'w+b') as derived:
                source.seek(0)
                offset = 0
                while True:
                    block = source.read(CHUNK_BYTES)
                    if not block:
                        break
                    derived.write(expected_block(block, offset, patch_offset, profile.replacement_word))
                    offset += len(block)
                require(offset == st.st_size, 'input size changed while copying')
                derived.flush()
                os.fsync(derived.fileno())
                source.seek(0)
                derived.seek(0)
                original_hash, derived_hash = hashlib.sha256(), hashlib.sha256()
                offset = 0
                while True:
                    original = source.read(CHUNK_BYTES)
                    copied = derived.read(CHUNK_BYTES)
                    require(copied == expected_block(original, offset, patch_offset,
                                                     profile.replacement_word),
                            'derived bytes differ outside the one allowed instruction word')
                    if not original:
                        break
                    original_hash.update(original)
                    derived_hash.update(copied)
                    offset += len(original)
                require(offset == st.st_size and original_hash.hexdigest() == original_sha256 and
                        identity(os.fstat(source.fileno())) == identity(st),
                        'source changed during copy/verification')
                derived_sha256 = derived_hash.hexdigest()
                require(derived_sha256 != original_sha256, 'derived instruction did not change the file')
            manifest = {
                'schema': 1,
                'status': 'VERIFIED_DERIVED_COPY_NOT_RUN',
                'created_utc': datetime.now(timezone.utc).isoformat(),
                'source': {'path': str(source_path), 'bytes': st.st_size, 'sha256': original_sha256},
                'derived': {'path': str(output_directory / 'main.nso'), 'bytes': st.st_size,
                            'sha256': derived_sha256, 'is_modified_text': True},
                'build_id_full_32_bytes': profile.build_id.lower(),
                'build_id_preserved': True,
                'patch': {'va': f'0x{profile.patch_va:08x}', 'file_offset': patch_offset,
                          'original_word_hex': profile.expected_word.hex(),
                          'replacement_word_hex': profile.replacement_word.hex(),
                          'instruction_bytes': 4,
                          'all_other_bytes_verified_equal': True},
                'context': {'va': f'0x{profile.context_va:08x}', 'file_offset': context_offset,
                            'bytes': profile.context_bytes, 'sha256': profile.context_sha256},
                'nso': {'flags': flags, 'text_file_offset': text[0], 'text_va': text[1],
                        'text_bytes': text[2], 'stored_header_hashes_preserved': True,
                        'text_hash_verification_flag': bool(flags & 8)},
                'attribution': {'author': 'OhhhhNoes', 'url': SOURCE_URL,
                                'published_version': 'US 1.0.1',
                                'published_build_id': '84E182F72EE1DA4F477804BD05B54C15',
                                'port': 'Same single-instruction interval change, separately guarded '
                                        'for the exact current BuildId and verified context; '
                                        'not the original published BuildId.'},
                'scope': 'Force the matched NVN interval-selection instruction to interval 1. '
                         'Only unlocks an opportunity for 60FPS. No game-clock edits; '
                         'logic/animation/audio speed equivalence and performance remain unverified. '
                         'Current native WSI ignores interval, '
                         'so this derived input alone need not improve FPS.',
                'integrity_note': 'Only the one text instruction changes. BuildId and stored NSO '
                                  'segment hashes are not rewritten; a consumer enforcing the '
                                  'original text hash may reject this explicitly derived copy. '
                                  'It must not be represented as an unmodified original module.',
            }
            manifest_fd = os.open('manifest.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
                                  dir_fd=directory_fd)
            with os.fdopen(manifest_fd, 'w') as report:
                json.dump(manifest, report, indent=2)
                report.write('\n')
                report.flush()
                os.fsync(report.fileno())
            os.fsync(directory_fd)
            return manifest
        finally:
            os.close(directory_fd)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path,
                        help='new directory inside this checkout .local/; parent must exist')
    args = parser.parse_args(argv)
    try:
        result = prepare(args.input, args.out, profile=PRODUCTION_PROFILE)
    except (OSError, ValueError) as error:
        print(f'60fps preparation refused: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'status': result['status'], 'derived': result['derived'],
                      'manifest': str(args.out.resolve() / 'manifest.json')}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
