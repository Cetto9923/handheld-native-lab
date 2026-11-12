#!/usr/bin/env python3
"""Compare two pipeline warm lists by the fields of their serialized keys.

Read-only. The lists hold the runtime's own pipeline recipes: the shader
bundle keys and the exact serialized create-info key of every pipeline the
device had to build. The key layout follows xbg_pipeline_key.h:

    kind(1=graphics,2=compute) | layout piece | render-pass piece |
    pipeline flags | subpass | stage count | per stage:
    flags | stage | shader module id(64) | ...state...

This tool decodes both lists and reports which fields differ, so a warm-up
that builds pipelines the runtime never asks for can be told apart from a
warm-up that is asked for and missed.

It does not establish Vulkan create-info validity, GPU support or performance.
"""
import argparse
import json
from pathlib import Path
import struct


def records(path):
    """Parse the framing: magic, stage count, shader keys, word count, words."""
    data = Path(path).read_bytes()
    at = 0
    out = []
    while at < len(data):
        if at + 8 > len(data):
            raise ValueError('truncated record header at %d' % at)
        magic, stages = struct.unpack_from('<II', data, at)
        at += 8
        if magic != 0x57504c58 or not 1 <= stages <= 5:
            raise ValueError('invalid record header at %d' % at)
        keys = []
        for _ in range(stages):
            (length,) = struct.unpack_from('<I', data, at)
            at += 4
            if not 16 <= length <= 256 or at + length > len(data):
                raise ValueError('invalid shader key length at %d' % at)
            keys.append(data[at:at + length].decode('ascii'))
            at += length
        (count,) = struct.unpack_from('<I', data, at)
        at += 4
        if not 1 <= count <= 4096 or at + count * 4 > len(data):
            raise ValueError('invalid word count at %d' % at)
        words = list(struct.unpack_from('<' + 'I' * count, data, at))
        at += count * 4
        out.append((keys, words))
    return out


def _piece(words, at):
    count = words[at]
    at += 1
    return words[at:at + count], at + count


def _render_pass(piece):
    """Decode the render-pass piece (xbg_renderpass_key)."""
    at = 0
    flags = piece[at]
    at += 1
    count = piece[at]
    at += 1
    attachments = []
    for _ in range(count):
        attachments.append(tuple(piece[at:at + 9]))
        at += 9
    subpass_flags = piece[at]
    at += 1
    bind_point = piece[at]
    at += 1
    color_refs = piece[at]
    at += 1
    at += 2 * color_refs
    has_depth = piece[at]
    at += 1
    if has_depth:
        at += 2
    dependencies = piece[at]
    at += 1
    at += 7 * dependencies
    return {
        'flags': flags,
        'attachments': attachments,
        'subpass_flags': subpass_flags,
        'bind_point': bind_point,
        'color_refs': color_refs,
        'has_depth': has_depth,
        'dependencies': dependencies,
    }


def _decode(words):
    kind = words[0]
    at = 1
    layout, at = _piece(words, at)
    if kind == 1:
        pass_piece, at = _piece(words, at)
        pipeline_flags = words[at]
        at += 1
        subpass = words[at]
        at += 1
        stage_count = words[at]
        at += 1
    else:
        pipeline_flags = words[at]
        at += 1
        subpass = None
        stage_count = 1
    stages = []
    for _ in range(stage_count):
        stage_flags = words[at]
        at += 1
        stage = words[at]
        at += 1
        module_id = words[at] | (words[at + 1] << 32)
        at += 2
        stages.append((stage_flags, stage, module_id))
    out = {'kind': 'graphics' if kind == 1 else 'compute',
           'layout_words': len(layout),
           'pipeline_flags': pipeline_flags,
           'subpass': subpass,
           'stages': stages}
    if kind == 1:
        info = _render_pass(pass_piece)
        out['render_pass'] = info
        out['render_pass_signature'] = (
            info['flags'], info['subpass_flags'], info['bind_point'],
            info['has_depth'], info['dependencies'],
            tuple((a[1], a[2], a[3], a[4], a[7], a[8]) for a in info['attachments']))
    return out


def summarize(path):
    parsed = records(path)
    render_passes = {}
    stages = {}
    layouts = {}
    kinds = {}
    for keys, words in parsed:
        decoded = _decode(words)
        kinds[decoded['kind']] = kinds.get(decoded['kind'], 0) + 1
        layouts[decoded['layout_words']] = layouts.get(decoded['layout_words'], 0) + 1
        if 'render_pass_signature' in decoded:
            signature = decoded['render_pass_signature']
            render_passes[signature] = render_passes.get(signature, 0) + 1
        for flags, stage, module_id in decoded['stages']:
            stages[(stage, module_id)] = stages.get((stage, module_id), 0) + 1
    return parsed, {'records': len(parsed), 'kinds': kinds,
                    'render_pass_signatures': render_passes,
                    'stage_module_pairs': stages,
                    'layout_word_counts': layouts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first', type=Path, help='the reference warm list')
    parser.add_argument('second', type=Path, help='the warm list to compare')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    first, first_summary = summarize(args.first)
    second, second_summary = summarize(args.second)
    first_pass = set(first_summary['render_pass_signatures'])
    second_pass = set(second_summary['render_pass_signatures'])
    first_stage = set(first_summary['stage_module_pairs'])
    second_stage = set(second_summary['stage_module_pairs'])
    first_words = {tuple(words) for _, words in first}
    second_words = {tuple(words) for _, words in second}
    report = {
        'first': {'path': str(args.first), 'records': first_summary['records'],
                  'kinds': first_summary['kinds'],
                  'render_pass_variants': len(first_pass),
                  'stage_module_pairs': len(first_stage),
                  'layout_word_counts': first_summary['layout_word_counts']},
        'second': {'path': str(args.second), 'records': second_summary['records'],
                   'kinds': second_summary['kinds'],
                   'render_pass_variants': len(second_pass),
                   'stage_module_pairs': len(second_stage),
                   'layout_word_counts': second_summary['layout_word_counts']},
        'shared_exact_keys': len(first_words & second_words),
        'only_in_first_exact_keys': len(first_words - second_words),
        'only_in_second_exact_keys': len(second_words - first_words),
        'shared_render_pass_variants': len(first_pass & second_pass),
        'only_in_first_render_pass_variants': len(first_pass - second_pass),
        'only_in_second_render_pass_variants': len(second_pass - first_pass),
        'shared_stage_module_pairs': len(first_stage & second_stage),
        'only_in_first_stage_module_pairs': len(first_stage - second_stage),
        'only_in_second_stage_module_pairs': len(second_stage - first_stage),
        'validation': 'record framing and key field decoding only; not GPU or performance acceptance',
    }
    print(json.dumps(report, indent=2))
    if args.out:
        args.out.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
