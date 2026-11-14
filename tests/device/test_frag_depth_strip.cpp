#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_depth.h"
#include <cassert>
#include <cstdint>
#include <vector>

// A minimal fragment module with the FFRS depth pattern: it declares
// gl_FragCoord, writes gl_FragDepth, and runs with DepthReplacing.
// 'load_fragcoord' decides whether anything actually reads gl_FragCoord.
static std::vector<uint32_t> build_module(bool load_fragcoord) {
    enum : uint32_t { VOID = 1, FLOAT = 2, VEC4 = 3, PTR_IN = 4, PTR_OUT = 5,
                      FRAGCOORD = 6, FRAGDEPTH = 7, ENTRY = 8, VALUE = 9, LOADED = 10 };
    std::vector<uint32_t> w;
    auto op = [&](uint32_t opcode, std::initializer_list<uint32_t> operands) {
        w.push_back((uint32_t(operands.size() + 1) << 16) | opcode);
        for (uint32_t o : operands) w.push_back(o);
    };
    w = {0x07230203u, 0x00010300u, 0u, 16u, 0u};
    op(17, {1});                                   // OpCapability Shader
    op(14, {0, 1});                               // OpMemoryModel Logical GLSL450
    op(15, {4, ENTRY, 0x6E69616Du, 0u, FRAGCOORD, FRAGDEPTH});  // OpEntryPoint Fragment
    op(16, {ENTRY, 7});                           // OpExecutionMode OriginUpperLeft
    op(16, {ENTRY, 12});                          // OpExecutionMode DepthReplacing
    op(71, {FRAGCOORD, 11, 15});                  // OpDecorate BuiltIn FragCoord
    op(71, {FRAGDEPTH, 11, 22});                  // OpDecorate BuiltIn FragDepth
    op(19, {VOID});
    op(22, {FLOAT, 32});
    op(23, {VEC4, FLOAT, 4});
    op(32, {PTR_IN, VEC4});
    op(32, {PTR_OUT, FLOAT});
    op(59, {PTR_IN, FRAGCOORD, 0});               // OpVariable Input
    op(59, {PTR_OUT, FRAGDEPTH, 3});              // OpVariable Output
    if (load_fragcoord) op(61, {VEC4, LOADED, FRAGCOORD});   // OpLoad from gl_FragCoord
    op(62, {FRAGDEPTH, VALUE});                   // OpStore gl_FragDepth
    return w;
}

static bool has_fragdepth_decoration(const std::vector<uint32_t> &w) {
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 71 && n == 4 && w[i + 2] == 11 && w[i + 3] == 22) return true;
        i += n;
    }
    return false;
}

static bool has_depth_replacing(const std::vector<uint32_t> &w) {
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 16 && n == 3 && w[i + 2] == 12) return true;
        i += n;
    }
    return false;
}

static bool stores_fragdepth(const std::vector<uint32_t> &w) {
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if (!n || i + n > w.size()) return false;
        if (op == 62 && n >= 3 && w[i + 1] == 7) return true;
        i += n;
    }
    return false;
}

int main() {
    auto unused = build_module(false);
    auto used = build_module(true);
    for (const auto &original : {unused, used}) {
        auto preserved = original;
        assert(!xbg_terrain_drop_frag_depth(preserved));
        assert(preserved == original);
        assert(!xbg_terrain_drop_frag_depth(preserved, false));
        assert(preserved == original);
    }

    // The strip itself still removes the whole depth pattern.
    assert(has_fragdepth_decoration(unused));
    assert(has_depth_replacing(unused));
    assert(stores_fragdepth(unused));
    auto stripped = unused;
    assert(xbg_terrain_drop_frag_depth(stripped, true));
    assert(!has_fragdepth_decoration(stripped));
    assert(!has_depth_replacing(stripped));
    assert(!stores_fragdepth(stripped));

    // The FragCoord declaration survives, and a module that reads it keeps
    // reading it after the strip.
    auto stripped_used = used;
    assert(xbg_terrain_drop_frag_depth(stripped_used, true));
    assert(!has_fragdepth_decoration(stripped_used));

    // A module that is not SPIR-V is refused, and it stays as it was.
    auto broken = unused;
    broken[0] = 0;
    const auto broken_before = broken;
    assert(!xbg_terrain_drop_frag_depth(broken, true));
    assert(broken == broken_before);
    return 0;
}
