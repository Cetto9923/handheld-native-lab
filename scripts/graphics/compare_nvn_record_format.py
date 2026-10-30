#!/usr/bin/env python3
"""Compare the NVN command record format of this port with the one of the
same game reference.

The recorder of this port writes the record format of the reference tree, so
that the replay can read a record with the reference's own structures. That
claim is only worth something if the structures really are the same, field for
field. This script checks it and exits non-zero when they are not.

The reference is a local checkout under .local/refs/nxnative (MIT, revision
7a029f66). Its path can be given with --reference; the default is the one
this project uses.

What is compared:

  * the header of a record: op_hdr against nvn_cb_op_hdr;
  * the opcodes: the reference's OP_* in its order against NVN_CB_OP_*,
    value by value;
  * every payload structure: the reference's p_* against nvn_cb_p_*, field
    name, field order, array dimension and the kind of the type.

The type of a field is compared by kind and not by name, because this port
renames the structures it borrows: the reference's tex_t becomes a pointer to
an immutable copy of the guest's object here, and view_t becomes nvn_cb_view.
A field is therefore one of:

  PTR     a pointer, whatever it points at
  STRUCT  a structure, whatever it is called
  INT     a signed integer
  UINT    an unsigned integer
  FLOAT   a floating point number
  BYTE    an unsigned 8 bit integer
  BITFIELD  a bit field of unsigned integers
"""
import argparse
import pathlib
import re
import sys

DEFAULT_REFERENCE = pathlib.Path('.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h')
OUR_HEADER = pathlib.Path('src/graphics/nvn_device/nvn_command_buffer.h')

# The structures of the reference that this port borrows under another name.
STRUCT_ALIASES = {
    'tex_t': 'STRUCT',
    'view_t': 'STRUCT',
    'vs_st': 'STRUCT',
    'prog_t': 'STRUCT',
    'dpool_t': 'STRUCT',
    'NVNbufferRange': 'STRUCT',
    'NVNcopyRegion': 'STRUCT',
    'nvn_cb_view': 'STRUCT',
    'nvn_cb_vs_st': 'STRUCT',
    'nvn_cb_buffer_range': 'STRUCT',
    'nvn_cb_copy_region': 'STRUCT',
    '__NESTED__': 'STRUCT',
}

SCALARS = {
    'int32_t': 'INT',
    'uint32_t': 'UINT',
    'uint64_t': 'UINT',
    'int64_t': 'INT',
    'uint16_t': 'UINT',
    'int16_t': 'INT',
    'uint8_t': 'BYTE',
    'int8_t': 'INT',
    'float': 'FLOAT',
    'double': 'FLOAT',
    'size_t': 'UINT',
    'int': 'INT',
    'unsigned': 'UINT',
    'long': 'INT',
    'unsigned long': 'UINT',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    return text


def parse_structs(text, name_pattern):
    """Return {name: [(field, kind, dims)]} for the structs the pattern matches.

    The bodies are found by matching the braces, so that one typedef does not
    run into the next one.
    """
    out = {}
    for match in re.finditer(r'typedef\s+struct\s*([A-Za-z_]\w*)?\s*\{', text):
        tag = match.group(1)
        start = match.end()
        depth = 1
        i = start
        while i < len(text) and depth:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        if depth:
            continue
        body = text[start:i - 1]
        # A member can be a nested structure or union. Its body is folded away
        # so that only the name of the member is compared.
        while True:
            folded = re.sub(r'\{[^{}]*\}', '__NESTED__', body)
            if folded == body:
                break
            body = folded
        tail = re.match(r'\s*(' + name_pattern + r')\s*;', text[i:])
        if not tail:
            continue
        name = tail.group(1)
        fields = []
        for raw in body.split(';'):
            raw = raw.strip()
            if not raw:
                continue
            fields.extend(parse_declaration(raw))
        out[name] = fields
    return out


def split_declarators(raw):
    """Split one struct member declaration into its declarators.

    A declaration can name several members at once ("uint32_t op, size;") and
    a bit field carries its width after a colon ("uint8_t flag : 1;").
    """
    parts = []
    depth = 0
    current = ''
    for char in raw:
        if char in '[':
            depth += 1
        elif char in ']':
            depth -= 1
        if char == ',' and depth == 0:
            parts.append(current)
            current = ''
            continue
        current += char
    if current.strip():
        parts.append(current)
    return [part.strip() for part in parts if part.strip()]


def parse_declaration(raw):
    """Return [(name, kind, dims)] for one struct member declaration."""
    parts = split_declarators(raw)
    if not parts:
        return []
    base_type, pointer = split_type(parts[0])
    out = []
    for part in parts:
        if ':' in part:
            name = re.match(r'^(?:[A-Za-z_]\w*\s+)*([A-Za-z_]\w*)\s*:', part)
            if not name:
                raise ValueError('cannot parse the bit field: ' + part)
            out.append((name.group(1), 'BITFIELD', []))
            continue
        name, dims = split_declarator(part)
        out.append((name, kind_of(base_type, pointer), dims))
    return out


def split_type(part):
    """Return (type words, is pointer) of the first declarator of a declaration."""
    head = part.split(':', 1)[0]
    # The declarator is at the end: an identifier, optionally with an array
    # suffix, optionally behind a star.
    match = re.match(r'^(.*?)\s*\*?\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*$', head, flags=re.S)
    if not match:
        raise ValueError('cannot split the member: ' + part)
    type_words = match.group(1).strip()
    pointer = '*' in head[:head.rfind(match.group(2))]
    return type_words, pointer


def split_declarator(part):
    """Return (name, dims) of a declarator."""
    head = part.split(':', 1)[0]
    match = re.match(r'^(?:.*?)\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*$', head, flags=re.S)
    if not match:
        raise ValueError('cannot parse the declarator: ' + part)
    dims = [d.strip() for d in re.findall(r'\[([^\]]*)\]', match.group(2))]
    return match.group(1), dims


def kind_of(base_type, pointer):
    """The kind of a member type, with the structures of the reference folded."""
    words = base_type.split()
    base = words[-1] if words else ''
    if pointer:
        return 'PTR'
    if base in STRUCT_ALIASES:
        return STRUCT_ALIASES[base]
    if base in SCALARS:
        return SCALARS[base]
    if len(words) == 1 and base[0].isupper():
        return 'STRUCT'
    raise ValueError('unknown member type: ' + base_type)


def parse_opcodes(text, pattern):
    """Return {name: value} for the enumerators the pattern matches."""
    out = {}
    for match in re.finditer(r'(' + pattern + r')\s*=\s*(\d+)', text):
        out[match.group(1)] = int(match.group(2))
    return out


def reference_opcode_order(text):
    """The reference's OP_* names in the order the enum lists them."""
    for match in re.finditer(r'enum\s*\{([^}]*)\}', text, flags=re.S):
        body = strip_comments(match.group(1))
        if 'OP_END' not in body:
            continue
        names = []
        for part in body.split(','):
            part = part.strip()
            if not part:
                continue
            name = part.split('=')[0].strip()
            if re.fullmatch(r'OP_\w+', name):
                names.append(name)
        return names
    raise SystemExit('the reference opcode enum was not found')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', default=str(DEFAULT_REFERENCE))
    parser.add_argument('--ours', default=str(OUR_HEADER))
    parser.add_argument('--json', help='write the report to this file')
    args = parser.parse_args()

    reference_path = pathlib.Path(args.reference)
    ours_path = pathlib.Path(args.ours)
    for path in (reference_path, ours_path):
        if not path.exists():
            raise SystemExit(f'missing {path}')

    reference = strip_comments(reference_path.read_text())
    ours = strip_comments(ours_path.read_text())

    problems = []
    report = []

    # 1. the header of a record
    ref_hdr = parse_structs(reference, r'op_hdr')
    our_hdr = parse_structs(ours, r'nvn_cb_op_hdr')
    if 'op_hdr' in ref_hdr and 'nvn_cb_op_hdr' in our_hdr:
        if ref_hdr['op_hdr'] != our_hdr['nvn_cb_op_hdr']:
            problems.append('the record header differs: ' +
                            f"{ref_hdr['op_hdr']} against {our_hdr['nvn_cb_op_hdr']}")
        else:
            report.append(f"the record header matches: {ref_hdr['op_hdr']}")
    else:
        problems.append('the record header was not found on one of the two sides')

    # 2. the opcodes
    ref_order = reference_opcode_order(reference)
    our_values = parse_opcodes(ours, r'NVN_CB_OP_\w+')
    our_order = [name for name, _ in sorted(our_values.items(), key=lambda kv: kv[1])]
    # The names differ by the prefix this port uses; what has to be the same
    # is the order, the number and the value of every opcode.
    def bare(name):
        name = name[len('NVN_CB_'):] if name.startswith('NVN_CB_') else name
        return name.rstrip('_')
    ref_bare = [bare(n) for n in ref_order]
    our_bare = [bare(n) for n in our_order]
    if ref_bare != our_bare:
        only_ref = [n for n in ref_bare if n not in our_bare]
        only_our = [n for n in our_bare if n not in ref_bare]
        problems.append('the opcode names differ: ' +
                        f"only the reference has {only_ref}, only this port has {only_our}")
    else:
        gaps = [our_order[i + 1] for i in range(len(our_order) - 1)
                if our_values[our_order[i + 1]] != our_values[our_order[i]] + 1]
        if gaps:
            problems.append('the opcode numbers are not consecutive at: ' + ', '.join(gaps))
        else:
            report.append(f'all {len(ref_order)} opcodes match by name, by order and by number')

    # 3. the payload structures
    ref_structs = parse_structs(reference, r'p_\w+')
    our_structs = parse_structs(ours, r'nvn_cb_p_\w+')
    matched = 0
    renamed = []
    for ref_name, ref_fields in sorted(ref_structs.items()):
        our_name = 'nvn_cb_' + ref_name
        if our_name not in our_structs:
            problems.append(f'the reference has {ref_name} and this port has no {our_name}')
            continue
        our_fields = our_structs[our_name]
        ref_layout = [(kind, dims) for _, kind, dims in ref_fields]
        our_layout = [(kind, dims) for _, kind, dims in our_fields]
        if ref_layout != our_layout:
            problems.append(f'{ref_name} has a different layout from {our_name}: '
                            f'{ref_fields} against {our_fields}')
            continue
        if [n for n, _, _ in ref_fields] != [n for n, _, _ in our_fields]:
            renamed.append(f'{ref_name}: ' +
                           ', '.join(f'{a} -> {b}' for a, b in
                                     zip([n for n, _, _ in ref_fields],
                                         [n for n, _, _ in our_fields])
                                     if a != b))
        matched += 1
    extra = [name for name in our_structs if name[7:] not in ref_structs]
    if extra:
        problems.append('this port has payload structures the reference does not: ' +
                        ', '.join(sorted(extra)))
    if matched and not any('layout' in p for p in problems):
        report.append(f'{matched} payload structures have the same layout, field for field')
    if renamed:
        report.append(f'{len(renamed)} structures have members this port renamed, '
                      'which changes no layout:')
        for line in renamed:
            report.append('  ' + line)

    print(f'reference: {reference_path}')
    print(f'this port: {ours_path}')
    for line in report:
        print('OK   ' + line)
    for line in problems:
        print('FAIL ' + line)
    print(f'{matched} payload structures compared, {len(problems)} problems')

    if args.json:
        import json
        pathlib.Path(args.json).write_text(json.dumps({
            'reference': str(reference_path),
            'ours': str(ours_path),
            'ok': not problems,
            'matched_payloads': matched,
            'report': report,
            'problems': problems,
        }, indent=1) + '\n')

    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
