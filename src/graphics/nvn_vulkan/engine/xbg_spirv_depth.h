#ifndef XBG_SPIRV_DEPTH_H
#define XBG_SPIRV_DEPTH_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

/**
 * Strips OpExecutionMode DepthReplacing and the redundant gl_FragDepth assignment
 * from a fragment shader.
 *
 * Background (measured on the FFRS shader set, 706 modules, 498 fragment
 * stages, 105 of them declaring gl_FragDepth):
 *   - Every one of those 105 shaders stores a depth value that is NOT the
 *     rasterizer's z. The stored value is computed from gl_FragCoord.z and
 *     gl_FragCoord.w together with push constants (the game's own depth
 *     remap), so dropping the store replaces the game's depth with the
 *     rasterizer's interpolated z -- a semantic change, never a no-op.
 *   - Declaring DepthReplacing also disables the rasterizer's hardware depth
 *     bias and Early-Z on Mali.
 *
 * The strip is therefore only allowed for a module that stores exactly
 * gl_FragCoord.z: there the value the rasterizer would write is the value the
 * shader writes, and dropping the store changes nothing but the early-Z
 * behaviour. xbg_spirv_fragdepth_stores_rasterizer_z() decides that, and on
 * the current FFRS shader set it answers false for every module.
 *
 * Note the earlier version of this header claimed the terrain shader
 * "statically stores gl_FragCoord.z into gl_FragDepth". That premise was
 * never verified and is false for the whole shader set; the measurement
 * above replaced it.
 */
inline bool xbg_terrain_drop_frag_depth(std::vector<uint32_t> &w) {
    if (w.size() < 5 || w[0] != 0x07230203u) return false;
    uint32_t frag_depth = 0;
    uint32_t entry_point = 0;
    for (size_t i = 5; i < w.size(); ) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 15 && w[i + 1] == 4) { // OpEntryPoint Fragment
            entry_point = w[i + 2];
        }
        if (op == 71 && n == 4 && w[i + 2] == 11 && w[i + 3] == 22) { // OpDecorate BuiltIn FragDepth
            frag_depth = w[i + 1];
        }
        i += n;
    }
    if (!frag_depth || !entry_point) return false;

    std::vector<uint32_t> out;
    out.reserve(w.size());
    out.insert(out.end(), w.begin(), w.begin() + 5);

    int dropped = 0;
    for (size_t i = 5; i < w.size(); ) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        // 1. OpEntryPoint: remove frag_depth from interface IDs
        if (op == 15 && w[i + 2] == entry_point) {
            std::vector<uint32_t> ep;
            ep.reserve(n);
            for (size_t j = 0; j < n; ++j) {
                if (j >= 5 && w[i + j] == frag_depth) continue;
                ep.push_back(w[i + j]);
            }
            ep[0] = (uint32_t(ep.size()) << 16) | 15u;
            out.insert(out.end(), ep.begin(), ep.end());
            i += n;
            continue;
        }
        // 2. OpExecutionMode %entry DepthReplacing (mode = 12)
        if (op == 16 && n == 3 && w[i + 1] == entry_point && w[i + 2] == 12) {
            ++dropped;
            i += n;
            continue;
        }
        // 3. OpDecorate %frag_depth BuiltIn FragDepth
        if (op == 71 && w[i + 1] == frag_depth) {
            ++dropped;
            i += n;
            continue;
        }
        // 4. OpVariable %type %frag_depth Output
        if (op == 59 && w[i + 2] == frag_depth) {
            ++dropped;
            i += n;
            continue;
        }
        // 5. OpStore %frag_depth %val
        if (op == 62 && w[i + 1] == frag_depth) {
            ++dropped;
            i += n;
            continue;
        }
        out.insert(out.end(), w.begin() + i, w.begin() + i + n);
        i += n;
    }
    if (dropped < 4) return false;
    w.swap(out);
    return true;
}


/**
 * Does this module store exactly gl_FragCoord.z into gl_FragDepth?
 *
 * That is the only case where dropping the store is a semantic no-op: the
 * rasterizer would write the same value. Any arithmetic between the load of
 * gl_FragCoord.z and the store -- the FFRS shaders remap the depth with
 * gl_FragCoord.w and push constants -- makes the store meaningful, and
 * dropping it replaces the game's depth with the rasterizer's.
 *
 * The check has to follow the pointer, not just look for a load of the
 * builtin variable: a GLSL compiler reaches gl_FragCoord.z through
 * OpAccessChain (gl_FragCoord is a vec4), so the value arrives as a load of
 * an access-chain result, never as a load of the builtin variable itself. A
 * check that only looks at OpLoad of the builtin therefore answers false for
 * every real shader.
 *
 * Conservative by construction: anything the walk does not understand counts
 * as "not the rasterizer's z", so the caller keeps the store.
 */
inline bool xbg_spirv_fragdepth_stores_rasterizer_z(const std::vector<uint32_t> &w) {
    if (w.size() < 5 || w[0] != 0x07230203u) return false;
    // SPIR-V opcodes used below.
    const uint32_t OP_CONSTANT = 43, OP_SPEC_CONSTANT = 50, OP_DECORATE = 71;
    const uint32_t OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62;
    const uint32_t OP_ACCESSCHAIN = 65, OP_INBOUNDS_ACCESSCHAIN = 66;
    const uint32_t OP_COMPOSITE_EXTRACT = 81, OP_COPY_OBJECT = 83;
    const uint32_t DEC_BUILTIN = 11, BI_FRAGCOORD = 15, BI_FRAGDEPTH = 22;

    uint32_t fragcoord = 0, fragdepth = 0;
    std::vector<uint32_t> constants;                 // id -> literal value
    std::vector<uint32_t> const_ids, const_vals;    // parallel arrays
    std::vector<uint32_t> ptr_root, ptr_index;       // id -> root var / component index
    std::vector<uint32_t> ptr_ids;                   // id list
    std::vector<uint32_t> load_ptr, load_ids;        // result id -> pointer id
    std::vector<uint32_t> copy_src, copy_ids;
    std::vector<uint32_t> extract_comp, extract_index, extract_ids;
    std::vector<uint32_t> store_ptr, store_obj;
    // Parallel (id, value) tables. Each table keeps its own id list, so a
    // lookup has to scan the list that belongs to the table.
    auto remember = [](std::vector<uint32_t> &ids, std::vector<uint32_t> &vals, uint32_t id, uint32_t v) {
        ids.push_back(id);
        vals.push_back(v);
    };
    for (size_t i = 5; i < w.size(); ) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == OP_DECORATE && n >= 4 && w[i + 2] == DEC_BUILTIN) {
            if (w[i + 3] == BI_FRAGCOORD) fragcoord = w[i + 1];
            else if (w[i + 3] == BI_FRAGDEPTH) fragdepth = w[i + 1];
        } else if ((op == OP_CONSTANT || op == OP_SPEC_CONSTANT) && n >= 4) {
            remember(const_ids, const_vals, w[i + 2], w[i + 3]);
        } else if (op == OP_VARIABLE && n >= 4) {
            ptr_ids.push_back(w[i + 2]);
            ptr_root.push_back(w[i + 2]);
            ptr_index.push_back(0xffffffffu); // no component index
        } else if ((op == OP_ACCESSCHAIN || op == OP_INBOUNDS_ACCESSCHAIN) && n >= 5) {
            uint32_t base = w[i + 3];
            uint32_t root = base, index = 0xffffffffu;
            for (size_t k = 0; k < ptr_ids.size(); ++k) {
                if (ptr_ids[k] == base) { root = ptr_root[k]; index = ptr_index[k]; break; }
            }
            // A component access chain carries one constant index (the first
            // index operand); take it so a nested chain keeps the component.
            // The operand is a constant id, so resolve it to its literal.
            if (n >= 5) {
                for (size_t k = 0; k < const_ids.size(); ++k)
                    if (const_ids[k] == w[i + 4]) { index = const_vals[k]; break; }
            }
            ptr_ids.push_back(w[i + 2]);
            ptr_root.push_back(root);
            ptr_index.push_back(index);
        } else if (op == OP_LOAD && n >= 4) {
            remember(load_ids, load_ptr, w[i + 2], w[i + 3]);
        } else if (op == OP_COPY_OBJECT && n >= 4) {
            remember(copy_ids, copy_src, w[i + 2], w[i + 3]);
        } else if (op == OP_COMPOSITE_EXTRACT && n >= 5) {
            uint32_t index = 0xffffffffu;
            for (size_t k = 0; k < const_ids.size(); ++k)
                if (const_ids[k] == w[i + 4]) { index = const_vals[k]; break; }
            remember(extract_ids, extract_comp, w[i + 2], w[i + 3]);
            remember(extract_ids, extract_index, w[i + 2], index);
        } else if (op == OP_STORE && n >= 3) {
            store_ptr.push_back(w[i + 1]);
            store_obj.push_back(w[i + 2]);
        }
        i += n;
    }
    if (!fragcoord || !fragdepth) return false;

    auto root_of = [&](uint32_t id) -> uint32_t {
        for (size_t k = 0; k < ptr_ids.size(); ++k) if (ptr_ids[k] == id) return ptr_root[k];
        return id;
    };
    auto index_of = [&](uint32_t id) -> uint32_t {
        for (size_t k = 0; k < ptr_ids.size(); ++k) if (ptr_ids[k] == id) return ptr_index[k];
        return 0xffffffffu;
    };
    auto is_fragcoord_z_pointer = [&](uint32_t ptr) -> bool {
        return root_of(ptr) == fragcoord && index_of(ptr) == 2; // gl_FragCoord.z
    };
    auto is_whole_fragcoord_pointer = [&](uint32_t ptr) -> bool {
        return root_of(ptr) == fragcoord && index_of(ptr) == 0xffffffffu;
    };

    // Does this value carry exactly gl_FragCoord.z?
    std::vector<uint32_t> value_stack;
    std::function<bool(uint32_t)> value_is_rasterizer_z = [&](uint32_t value) -> bool {
        if (value_stack.size() > 64) return false;
        value_stack.push_back(value);
        struct Pop { std::vector<uint32_t> *v; ~Pop() { v->pop_back(); } } pop{&value_stack};
        for (size_t k = 0; k < load_ids.size(); ++k) {
            if (load_ids[k] != value) continue;
            if (is_fragcoord_z_pointer(load_ptr[k])) return true;
        }
        for (size_t k = 0; k < copy_ids.size(); ++k) {
            if (copy_ids[k] != value) continue;
            if (value_is_rasterizer_z(copy_src[k])) return true;
        }
        for (size_t k = 0; k < extract_ids.size(); ++k) {
            if (extract_ids[k] != value) continue;
            if (extract_index[k] != 2) continue; // a component other than .z
            for (size_t l = 0; l < load_ids.size(); ++l) {
                if (load_ids[l] != extract_comp[k]) continue;
                if (is_whole_fragcoord_pointer(load_ptr[l])) return true;
            }
        }
        return false;
    };

    for (size_t k = 0; k < store_ptr.size(); ++k) {
        if (root_of(store_ptr[k]) != fragdepth) continue;
        if (value_is_rasterizer_z(store_obj[k])) return true;
    }
    return false;
}

#endif // XBG_SPIRV_DEPTH_H
