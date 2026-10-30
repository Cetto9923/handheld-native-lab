#!/usr/bin/env python3
"""One-off transformation: give every NVN object family a table that grows.

The families of NVN objects (a sampler, a texture, a texture view, a blend
state, a render state, a program and the two builders) kept their host side
records in a fixed array of a few dozen slots. The device run of p-mm-43 showed
that a real game does not fit in that: the guest asked for more than sixteen
hundred blend states and more than four thousand render states in the first
seconds, and the families refused the ones that did not fit.

This script rewrites the eight families to use the shared table of
src/graphics/nvn_device/nvn_object_table.h, which grows with the number of
objects that are live. The transformation is mechanical and the same for every
family, which is why it is done here instead of by hand eight times.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path('src/graphics/nvn_device')

# file, entry type, table variable, key field, cap macro, new cap, how a new
# entry is initialised, the family specific name of the cap
FAMILIES = [
    dict(file='nvn_blend_state.c', type='nvn_blend_state_value', table='s_states',
         key='state', cap='NVN_BLEND_STATE_MAX_STATES', new_cap=65536,
         init='memset(free_slot, 0, sizeof(*free_slot));', newname='NVN_BLEND_STATE_MAX_OBJECTS',
         comment='the blend states the host side table holds at most'),
    dict(file='nvn_render_state.c', type='nvn_rs_value', table='s_states',
         key=None, cap='NVN_RS_MAX_STATES', new_cap=65536,
         init='memset(free_slot, 0, sizeof(*free_slot));', newname='NVN_RS_MAX_OBJECTS',
         comment='the render states the host side table holds at most'),
    dict(file='nvn_sampler.c', type='nvn_sampler_state', table='s_samplers',
         key='sampler', cap='NVN_SAMPLER_MAX_SAMPLERS', new_cap=65536,
         header='nvn_sampler.h',
         init='*free_slot = default_state();', newname='NVN_SAMPLER_MAX_OBJECTS',
         comment='the samplers the host side table holds at most'),
    dict(file='nvn_texture.c', type='nvn_texture_state', table='s_textures',
         key='texture', cap='NVN_TEXTURE_MAX_TEXTURES', new_cap=1048576,
         header='nvn_texture.h',
         init='*free_slot = default_state();', newname='NVN_TEXTURE_MAX_OBJECTS',
         comment='the textures the host side table holds at most'),
    dict(file='nvn_texture_view.c', type='nvn_texture_view_state', table='s_views',
         key='view', cap='NVN_TEXTURE_VIEW_MAX_VIEWS', new_cap=1048576,
         header='nvn_texture_view.h',
         init='*free_slot = default_state();', newname='NVN_TEXTURE_VIEW_MAX_OBJECTS',
         comment='the texture views the host side table holds at most'),
    dict(file='nvn_program.c', type='nvn_program_state', table='s_programs',
         key='program', cap='NVN_PROGRAM_MAX_PROGRAMS', new_cap=4096,
         init='memset(free_slot, 0, sizeof(*free_slot));', newname='NVN_PROGRAM_MAX_OBJECTS',
         comment='the programs the host side table holds at most'),
    dict(file='nvn_sampler_builder.c', type='nvn_sampler_builder_state', table='s_builders',
         key='builder', cap='NVN_SAMPLER_BUILDER_MAX_BUILDERS', new_cap=65536,
         header='nvn_sampler_builder.h',
         init='*free_slot = default_state();', newname='NVN_SAMPLER_BUILDER_MAX_OBJECTS',
         comment='the sampler builders the host side table holds at most'),
    dict(file='nvn_texture_builder.c', type='nvn_texture_builder_state', table='s_builders',
         key='builder', cap='NVN_TEXTURE_BUILDER_MAX_BUILDERS', new_cap=65536,
         header='nvn_texture_builder.h',
         init='*free_slot = default_state();', newname='NVN_TEXTURE_BUILDER_MAX_OBJECTS',
         comment='the texture builders the host side table holds at most'),
]

FIND_TEMPLATE = '''static {type} *{fname}({args}) {{
    size_t i;
    {type} *free_slot = NULL;
    if (!{key}) return NULL;
    for (i = 0; i < {table}.count; i++) {{
        {type} *entry = ({type} *)nvn_object_table_at(&{table}, i);
        if (!entry) continue;
        if (entry->in_use) {{
            if ({match}) return entry;
        }} else if (!free_slot) {{
            free_slot = entry;
        }}
    }}
    if (!create) return NULL;
    if (!free_slot) {{
        free_slot = ({type} *)nvn_object_table_new_entry(&{table}, {cap});
        if (!free_slot) return NULL;
    }}
    {init}
    {assign}
    free_slot->in_use = 1;
    return free_slot;
}}'''

for spec in FAMILIES:
    path = ROOT / spec['file']
    src = path.read_text()
    cap = spec['cap']
    table = spec['table']
    typ = spec['type']

    if spec['key'] is None:
        # The render state family keys on the object and the family of the
        # state at the same time.
        fname = 'state_find'
        args = 'const void *object, int family, int create'
        key = 'object'
        match = 'entry->object == object && entry->family == family'
        assign = 'free_slot->object = (void *)(uintptr_t)object;\n    free_slot->family = family;\n    apply_defaults(free_slot, family);'
    else:
        fname = spec['key'] + '_find'
        args = 'void *%s, int create' % spec['key']
        key = spec['key']
        match = 'entry->%s == %s' % (spec['key'], spec['key'])
        assign = 'free_slot->%s = %s;' % (spec['key'], spec['key'])

    # 1. The cap becomes a limit and not the size of an array. It lives in the
    #    header of the family when the family has one.
    cap_file = ROOT / spec['header'] if spec.get('header') else path
    cap_src = cap_file.read_text()
    old_cap_line = re.search(r'^#define %s .*\n' % cap, cap_src, re.M)
    if not old_cap_line:
        sys.exit('%s: the cap %s is not there' % (cap_file, cap))
    cap_src = cap_src[:old_cap_line.start()] + ('#define %s %d\n' % (spec['newname'], spec['new_cap'])) + cap_src[old_cap_line.end():]
    cap_src = cap_src.replace(cap, spec['newname'])
    if cap_file is path:
        # The cap is in the file itself: the change belongs to the text that
        # is written back at the end, not to a copy of it.
        src = cap_src
    else:
        cap_file.write_text(cap_src)
        src = src.replace(cap, spec['newname'])

    # 2. The array becomes a table that grows.
    old_array = re.search(r'^static %s %s\[%s\];\n' % (typ, table, spec['newname']), src, re.M)
    if not old_array:
        sys.exit('%s: the array of %s is not there' % (spec['file'], typ))
    src = src[:old_array.start()] + (
        'static nvn_object_table %s = { NULL, 0, 0, sizeof(%s) };\n' % (table, typ)) + src[old_array.end():]

    # 3. The find function grows with the table.
    old_find = re.search(
        r'^static %s \*%s\(.*?^\}\n' % (typ, fname), src, re.M | re.S)
    if not old_find:
        sys.exit('%s: the find function is not there' % spec['file'])
    new_find = FIND_TEMPLATE.format(type=typ, fname=fname, args=args, key=key,
                                    match=match, table=table, cap=spec['newname'],
                                    init=spec['init'], assign=assign) + '\n'
    src = src[:old_find.start()] + new_find + src[old_find.end():]

    # 4. The counters walk the table.
    src = re.sub(r'for \(i = 0; i < %s; i\+\+\)' % spec['newname'],
                 'for (i = 0; i < %s.count; i++)' % table, src)
    src = src.replace('%s[i].' % table, '((%s *)nvn_object_table_at(&%s, i))->' % (typ, table))

    # 5. The include of the shared table.
    if '#include "nvn_object_table.h"' not in src:
        includes = list(re.finditer(r'^#include [^\n]*\n', src, re.M))
        last = includes[-1]
        src = src[:last.end()] + '#include "nvn_object_table.h"\n' + src[last.end():]

    path.write_text(src)
    print('%s: the table of %s grows, at most %d entries' % (spec['file'], typ, spec['new_cap']))
