#!/usr/bin/env python3
"""FF Resonance (Switch/Android port) save tool: decrypt, inspect, and edit unit stats.

SAVE FORMAT (reverse engineered from the game's own NSO and verified on three
real save files produced by the port)

    0x00  4   magic 'SVHD'
    0x04  4   uint32 format version (0x00010000)
    0x08  20  SHA-1 of the ciphertext body
    0x1C  4   reserved (zero)
    0x20  ..  AES-256-ECB ciphertext, no IV, no chaining, no padding

The key is the 32 ASCII bytes of the FString property
USaveGameUtilitySettings::SaveDataEncryptionKey from the shipped
DefaultGame.ini. It is NOT stored in this file: pass --key-file or set
FFRS_SAVE_KEY, or drop a save_key.txt next to the tool.

The plaintext is a UE5 tagged property blob (class
/Script/mucha.CPP_MuchaGameSaveData). Inside it the unit roster lives in
'unitParameterList', an array of SaveData_UnitParameter structs. Each struct
carries UnitId, Experience, HitPoint and MagicPoint as IntProperty tags; the
per-unit maximums are NOT stored (the engine derives them from the unit level
and the equipped gear, see eDataTableType::UnitLevelParameterList), so the
tool raises the level through Experience and raises the current hit points
directly.

Usage
    python3 ffrs_save_tool.py list   <savefile>
    python3 ffrs_save_tool.py cheat  <savefile> -o out.bin [options]

Cheat options
    --hp N        current HitPoint for every unit        (default 99999)
    --mp N        MagicPoint for every unit              (default 99999)
    --max-hp N    guest/party HitPoint cap               (default 99999)
    --max-mp N    guest/party MagicPoint cap             (default 99999)
    --exp N       Experience for every unit              (default: unchanged)
    --unit N      only touch this UnitId (default: all)
    --push        install the edited slot on the tablet
    --device ID   adb device id for --push
    --restart     relaunch the game after the swap
"""
import argparse
import hashlib
import os
import struct
import sys
import time

HEADER_LEN = 32
MAGIC = b'SVHD'
DEFAULT_VERSION = 0x00010000
DEFAULT_DEVICE = 'A42BBB5109000333'
DEFAULT_PKG = 'org.ffrs.gamewindow'
SAVE_DIR = '/sdcard/Android/data/%s/files/codex-local-save' % DEFAULT_PKG

# Tag names this tool understands inside a unit struct.
UNIT_INT_TAGS = ('UnitId', 'Experience', 'HitPoint', 'MagicPoint')


def load_key(explicit=None):
    """Return the 32 byte AES key."""
    candidates = []
    if explicit:
        candidates.append(explicit)
    env = os.environ.get('FFRS_SAVE_KEY')
    if env:
        candidates.append(env)
    here = os.path.dirname(os.path.abspath(__file__))
    candidates.append(os.path.join(here, 'save_key.txt'))
    candidates.append(os.path.join(here, '..', '..', '.local', 'cheat', 'save_key.txt'))
    for path in candidates:
        if not path or not os.path.isfile(path):
            continue
        raw = open(path, 'rb').read().strip()
        if len(raw) == 32:
            return raw
        raise SystemExit('key file %s holds %d bytes, expected 32' % (path, len(raw)))
    raise SystemExit(
        'no AES key found. Put the 32 byte SaveDataEncryptionKey into '
        '.local/cheat/save_key.txt, pass --key-file, or set FFRS_SAVE_KEY.')


def aes_ecb(key, data, decrypt):
    try:
        from Crypto.Cipher import AES
    except ImportError:
        try:
            from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        except ImportError:
            raise SystemExit('need pycryptodome or cryptography for AES')
        cipher = Cipher(algorithms.AES(key), modes.ECB())
        ctx = cipher.decryptor() if decrypt else cipher.encryptor()
        return ctx.update(data) + ctx.finalize()
    cipher = AES.new(key, AES.MODE_ECB)
    return cipher.decrypt(data) if decrypt else cipher.encrypt(data)


def split_save(raw):
    if raw[:4] != MAGIC:
        raise ValueError('not an SVHD save (magic %r)' % raw[:4])
    version = struct.unpack_from('<I', raw, 4)[0]
    digest = raw[8:28]
    body = raw[HEADER_LEN:]
    return version, digest, body


def decrypt_save(raw, key):
    version, digest, body = split_save(raw)
    plain = bytearray(aes_ecb(key, body, True))
    ok = hashlib.sha1(body).digest() == digest
    return version, ok, plain


def encrypt_save(plain, key, version=DEFAULT_VERSION):
    body = aes_ecb(key, plain, False)
    return MAGIC + struct.pack('<I', version) + hashlib.sha1(body).digest() + b'\x00' * 4 + body


def _read_tag(buf, off):
    """Decode one tag header.

    Layout: int32 name_len (includes the NUL), ANSI name, int32 type_len
    (includes the NUL), ANSI type, int32 arr_idx, int32 size, then the
    value. IntProperty carries one extra byte (a per-property GUID flag)
    between the size and the int32 value.
    """
    if off + 8 > len(buf):
        return None
    name_len = struct.unpack_from('<i', buf, off)[0]
    if not 1 <= name_len <= 80:
        return None
    name = buf[off + 4:off + 4 + name_len - 1]
    if not name or name_len < 2:
        return None
    p = off + 4 + name_len
    if p + 4 > len(buf):
        return None
    type_len = struct.unpack_from('<i', buf, p)[0]
    if not 1 <= type_len <= 60:
        return None
    typ = buf[p + 4:p + 4 + type_len - 1]
    q = p + 4 + type_len
    if q + 8 > len(buf):
        return None
    array_index, size = struct.unpack_from('<ii', buf, q)
    return {'name': name.decode('utf8', 'replace'),
            'type': typ.decode('utf8', 'replace'),
            'name_off': off + 4,
            'array_index': array_index,
            'size': size,
            'value_off': q + 8}


def find_int_tags(buf, wanted=None):
    """Return every IntProperty tag with its int32 value and value offset."""
    out = []
    off = 0
    limit = len(buf) - 16
    while off < limit:
        tag = _read_tag(buf, off)
        if tag is None:
            off += 1
            continue
        if tag['type'] != 'IntProperty':
            off += 1
            continue
        value_off = tag['value_off'] + 1          # the GUID flag byte
        if value_off + 4 > len(buf):
            off += 1
            continue
        value = struct.unpack_from('<i', buf, value_off)[0]
        if wanted is None or tag['name'] in wanted:
            out.append((tag['name'], value, value_off))
        off += 1
    return out


def group_units(buf):
    """Group the unit roster tags into per-unit records.

    The roster is written unit by unit (UnitId, Experience, HitPoint,
    MagicPoint, ...), so the tags appear in that order.
    """
    tags = find_int_tags(buf, set(UNIT_INT_TAGS))
    units = []
    current = {}
    for name, value, off in tags:
        if name == 'UnitId':
            if current:
                units.append(current)
            current = {'UnitId': value, '_offset': off}
        elif current:
            current.setdefault(name, value)
            current.setdefault(name + '_off', off)
    if current:
        units.append(current)
    return units


def read_caps(buf):
    """Every HitPoint/MagicPoint int in the file, in order.

    The first eight are the roster units' current values; the extra pair at
    the end belongs to the guest/party record (unitGuest -> Party ->
    partyList -> SaveData_UnitGuest).
    """
    caps = {}
    for name, value, off in find_int_tags(buf, {'HitPoint', 'MagicPoint'}):
        caps.setdefault(name, []).append((value, off))
    return caps


def push_to_device(path, device, restart):
    """Install the edited save slot on the tablet and verify it landed."""
    import subprocess

    def adb(*argv):
        cmd = ['adb', '-s', device] + list(argv)
        return subprocess.run(cmd, capture_output=True, text=True)

    # The game rewrites the slot when it exits, so stop it before the swap.
    adb('shell', 'am', 'force-stop', DEFAULT_PKG)
    time.sleep(1.5)
    res = adb('push', path, '%s/GameSaveData0' % SAVE_DIR)
    if res.returncode != 0:
        raise SystemExit('adb push failed: %s' % res.stderr.strip())
    adb('shell', 'chmod', '0644', '%s/GameSaveData0' % SAVE_DIR)
    local = hashlib.md5(open(path, 'rb').read()).hexdigest()
    res = adb('shell', 'md5sum', '%s/GameSaveData0' % SAVE_DIR)
    remote = res.stdout.strip().split()[0] if res.stdout.strip() else ''
    if remote != local:
        raise SystemExit('device md5 %s does not match local %s' % (remote, local))
    print('pushed to %s  (md5 %s verified)' % (device, local))
    if restart:
        adb('shell', 'monkey', '-p', DEFAULT_PKG,
            '-c', 'android.intent.category.LAUNCHER', '1')
        print('restarted the game; press A on Continue to load the slot')


def cmd_list(args):
    raw = open(args.savefile, 'rb').read()
    version, ok, plain = decrypt_save(raw, load_key(args.key_file))
    print('file      : %s' % args.savefile)
    print('size      : %d  version=0x%08x  sha1_ok=%s' % (len(raw), version, ok))
    if not ok:
        print('WARNING    : the stored SHA-1 does not match the body')
    units = group_units(plain)
    print('%-8s %-12s %-12s %-12s' % ('UnitId', 'HitPoint', 'MagicPoint', 'Experience'))
    for u in units:
        print('%-8d %-12d %-12d %-12d' % (u.get('UnitId', -1),
                                         u.get('HitPoint', -1),
                                         u.get('MagicPoint', -1),
                                         u.get('Experience', -1)))
    caps = read_caps(plain)
    if caps:
        print('HitPoint/MagicPoint in file (roster units, then guest/party cap):')
        for name, items in caps.items():
            print('  %-11s %s' % (name, [v for v, _ in items]))


def cmd_cheat(args):
    key = load_key(args.key_file)
    raw = open(args.savefile, 'rb').read()
    version, ok, plain = decrypt_save(raw, key)
    if not ok:
        raise SystemExit('refusing to edit: the stored SHA-1 does not match the body')
    units = group_units(plain)
    edits = []

    def put(unit, tag, value):
        off = unit.get(tag + '_off')
        if off is None:
            return
        old = struct.unpack_from('<i', plain, off)[0]
        if old != value:
            struct.pack_into('<i', plain, off, value)
            edits.append((unit.get('UnitId'), tag, old, value))

    for unit in units:
        if args.unit is not None and unit.get('UnitId') != args.unit:
            continue
        if args.hp is not None:
            put(unit, 'HitPoint', args.hp)
        if args.mp is not None:
            put(unit, 'MagicPoint', args.mp)
        if args.exp is not None:
            put(unit, 'Experience', args.exp)

    # The guest/party caps live in the guest record, which sits after the
    # whole unitParameterList roster.
    if args.max_hp is not None or args.max_mp is not None:
        wanted = set()
        if args.max_hp is not None:
            wanted.add('HitPoint')
        if args.max_mp is not None:
            wanted.add('MagicPoint')
        unit_offs = [u[t + '_off'] for u in units for t in UNIT_INT_TAGS
                     if t + '_off' in u]
        roster_end = max(unit_offs) if unit_offs else -1
        cap_tags = [(n, v, o) for n, v, o in find_int_tags(plain, wanted)
                    if o > roster_end]
        cap_names = [n for n, _, _ in cap_tags]
        if sorted(cap_names) != sorted(wanted):
            raise SystemExit(
                'guest/party cap block is ambiguous (%s); refusing to edit'
                % cap_names)
        for name, value, off in cap_tags:
            new = args.max_hp if name == 'HitPoint' else args.max_mp
            old = struct.unpack_from('<i', plain, off)[0]
            if old != new:
                struct.pack_into('<i', plain, off, new)
                edits.append(('guest/party', name, old, new))

    if not edits:
        print('nothing to change')
    else:
        print('edits:')
        for unit_id, tag, old, new in edits:
            print('  unit %-6s %-11s %d -> %d' % (unit_id, tag, old, new))

    out = encrypt_save(plain, key, version)
    if len(out) != len(raw):
        raise SystemExit('refusing to write: size changed %d -> %d' % (len(raw), len(out)))
    with open(args.out, 'wb') as fh:
        fh.write(out)

    # Round trip check on the produced file.
    version2, ok2, plain2 = decrypt_save(out, key)
    if not ok2 or plain2 != plain:
        raise SystemExit('round trip check failed')
    print('wrote %s  (%d bytes, sha1_ok=True, round trip ok)' % (args.out, len(out)))

    if args.push:
        push_to_device(out, args.device, args.restart)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)

    p_list = sub.add_parser('list', help='show the unit roster')
    p_list.add_argument('savefile')
    p_list.add_argument('--key-file')
    p_list.set_defaults(func=cmd_list)

    p_cheat = sub.add_parser('cheat', help='edit unit stats and re-encrypt')
    p_cheat.add_argument('savefile')
    p_cheat.add_argument('-o', '--out', required=True)
    p_cheat.add_argument('--key-file')
    p_cheat.add_argument('--hp', type=int)
    p_cheat.add_argument('--mp', type=int)
    p_cheat.add_argument('--max-hp', type=int)
    p_cheat.add_argument('--max-mp', type=int)
    p_cheat.add_argument('--exp', type=int)
    p_cheat.add_argument('--unit', type=int)
    p_cheat.add_argument('--push', action='store_true',
                         help='install the edited slot on the tablet')
    p_cheat.add_argument('--device', default=DEFAULT_DEVICE)
    p_cheat.add_argument('--restart', action='store_true',
                         help='launch the game again after the swap')
    p_cheat.set_defaults(func=cmd_cheat)

    args = ap.parse_args(argv)
    args.func(args)


if __name__ == '__main__':
    main()
