"""Read existing XGS archive; audit actual source types without shader downloads."""
import hashlib
import json
from pathlib import Path
import struct
import sys

keys = {
    '26aef111ba3b3c4e': 12, '8b27994fdc0e32c9': 12,
    '9528f530e1c95ce5': 12, 'be4e89a0687ae37c': 18,
    '431a6183c3565f66': 18, '1e222af40b26bd25': 18,
    'b0284572b5f180f2': 18, '6018ff2b44b763d7': 18,
}
result = []
with Path(sys.argv[1]).open('rb') as f:
    magic, count, reserved = struct.unpack('<8sII', f.read(16))
    assert magic == b'HNLXGS1\0' and reserved == 0
    index = {}
    for _ in range(count):
        key, offset, size = struct.unpack('<16sQQ', f.read(32))
        index[key.decode()] = (offset, size)
    for key, base in keys.items():
        offset, size = index[key]
        f.seek(offset)
        data = f.read(size)
        loose = Path(sys.argv[3]) / (key + '.xgs') if len(sys.argv) > 3 else None
        source = 'archive'
        if loose is not None and loose.is_file():
            data = loose.read_bytes()
            source = 'loose'
        magic, version, stage, nb, ni, c1, ns, reserved = struct.unpack_from('<8I', data)
        assert (magic, version, stage) == (0x31534758, 1, 0)
        assert len(data) == 32 + nb * 32 + ni * 4 + c1 + ns
        bindings = [struct.unpack_from('<8I', data, 32 + i * 32) for i in range(nb)]
        spv = data[-ns:]
        words = struct.unpack('<%dI' % (ns // 4), spv)
        assert words[0] == 0x07230203
        types, variables, decorated = {}, {}, {}
        i = 5
        while i < len(words):
            n, op = words[i] >> 16, words[i] & 65535
            assert n and i + n <= len(words)
            a = words[i + 1:i + n]
            if op in (21, 22, 25, 27, 28, 29, 32):
                types[a[0]] = (op, a[1:])
            if op == 59:
                variables[a[1]] = a[0]
            if op == 71 and a[1] == 33:
                decorated[a[0]] = a[2]
            i += n
        sampled = {}
        for variable, binding in decorated.items():
            t = types[variables[variable]]
            if t[0] != 32:
                continue
            t = types.get(t[1][1])
            if t is None:  # Non-image buffer structs and scalar/vector inputs.
                continue
            while t[0] in (27, 28, 29):
                t = types[t[1][0]]
            if t[0] == 25 and t[1][1] == 5:  # DimBuffer
                sampled[binding] = types[t[1][0]]
        half = next(b for b in bindings if b[0] == base + 6)
        assert half[1] == 4 and half[4] == 7, half
        assert ((half[7] >> 16) & 3) == 0
        assert sampled[base + 6] == (22, (32,)), sampled
        result.append(dict(key=key, source=source, archive_offset=offset, archive_size=size,
                           half_binding=base + 6, source_slot=half[5],
                           source_metadata_scalar=0, original_sampled_type='float32',
                           other_raw_bindings=[base, base + 12],
                           xgs_sha256=hashlib.sha256(data).hexdigest(),
                           original_spirv_sha256=hashlib.sha256(spv).hexdigest()))
Path(sys.argv[2]).write_text(json.dumps(result, indent=2) + '\n')
print('Eight actual XGS half descriptors: Float32 SPIR-V / scalar0 PASS; source slots recorded individually')
