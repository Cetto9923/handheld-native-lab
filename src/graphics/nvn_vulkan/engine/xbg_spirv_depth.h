#ifndef XBG_SPIRV_DEPTH_H
#define XBG_SPIRV_DEPTH_H

#include <cstddef>
#include <cstdint>
#include <vector>

// The original FFRS shader computes its own depth. Keep its bytes by default.
// Explicit diagnostic runs can remove the output to reproduce the old regression.
inline bool xbg_terrain_drop_frag_depth(std::vector<uint32_t> &w, bool explicitly_requested = false) {
    if (!explicitly_requested || w.size() < 5 || w[0] != 0x07230203u) return false;
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


#endif // XBG_SPIRV_DEPTH_H
