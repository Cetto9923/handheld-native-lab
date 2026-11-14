#ifndef XBG_SPIRV_DEPTH_H
#define XBG_SPIRV_DEPTH_H

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Strips OpExecutionMode DepthReplacing and the redundant gl_FragDepth assignment
 * from the FFRS terrain fragment shader (062df44b75d37d76).
 *
 * Background:
 * The terrain shader statically stores gl_FragCoord.z into gl_FragDepth. On Arm
 * Mali GPUs (e.g. Mali-G615 on Dimensity 8300 / RG477M), declaring DepthReplacing
 * disables the rasterizer's hardware depth bias and disables Early-Z / FPK. When
 * base terrain chunks and decal/overlay chunks share identical depth coordinates
 * with LEQUAL depth comparison, multithreaded command recording inversions cause
 * a violent 30Hz ground flickering defect.
 *
 * Stripping gl_FragDepth restores Mali hardware depth bias and re-enables Early-Z.
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
 * Does the module read gl_FragCoord (BuiltIn 15) at all?
 *
 * A shader that stores gl_FragCoord.z into gl_FragDepth writes exactly the
 * value the rasterizer would have written, so dropping the store is a no-op
 * for it. A shader that computes its own depth value does not read
 * gl_FragCoord, and for it dropping the store replaces the game's depth
 * with the rasterizer's interpolated z -- a semantic change that breaks the
 * depth test against everything the game drew with the usual convention.
 *
 * The FFRS shader 062df44b75d37d76 (the one that draws the protagonist) is
 * such a shader: it never reads gl_FragCoord, and the strip made the
 * protagonist lose whole horizontal bands to the depth test.
 */
inline bool xbg_spirv_reads_fragcoord(const std::vector<uint32_t> &w) {
    if (w.size() < 5 || w[0] != 0x07230203u) return false;
    uint32_t fragcoord = 0;
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 71 && n >= 4 && w[i + 2] == 11 && w[i + 3] == 15) fragcoord = w[i + 1];  // OpDecorate BuiltIn FragCoord
        i += n;
    }
    if (!fragcoord) return false;
    // The declaration alone proves nothing: the translator lists every builtin
    // the shader could use. Only a load shows that the value is read.
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 61 && n >= 4 && w[i + 3] == fragcoord) return true;  // OpLoad from the FragCoord variable
        i += n;
    }
    return false;
}

#endif // XBG_SPIRV_DEPTH_H
