#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_depth.h"
#include <cassert>
#include <cstdint>
#include <vector>

// Minimal fragment modules.
//
// The FFRS shaders reach gl_FragCoord through OpAccessChain, because
// gl_FragCoord is a vec4: the value arrives as a load of an access-chain
// result, never as a load of the builtin variable itself. The predicate has
// to follow that pointer, so the fixtures below build the same shape.
//
// The three cases:
//   stores_z       -- stores gl_FragCoord.z  => the strip is a no-op
//   stores_remap   -- stores a remapped depth (fc.z and fc.w combined with a
//                      push constant) => the strip changes the depth value
//   stores_literal -- stores a plain number  => the strip changes the depth value
enum : uint32_t { VOID = 1, FLOAT = 2, VEC4 = 3, PTR_IN = 4, PTR_OUT = 5,
                  FRAGCOORD = 6, FRAGDEPTH = 7, ENTRY = 8, VALUE = 9,
                  LOADED = 10, CHAIN = 11, CONST_Z = 12, CONST_W = 13,
                  PUSH = 14, CONST_2 = 15, CONST_3 = 16, CONST_0 = 17,
                  PTR_IN_F = 18, CHAIN_F = 19, CONST_1 = 20 };

enum class Shape { StoresZ, StoresRemap, StoresLiteral };

static std::vector<uint32_t> build_module(Shape shape) {
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
    op(32, {PTR_IN, FLOAT});                      // a pointer to one vec4 component
    op(32, {PTR_OUT, FLOAT});
    op(43, {FLOAT, CONST_0, 0});                  // OpConstant 0
    op(43, {FLOAT, CONST_1, 1});                  // OpConstant 1
    op(43, {FLOAT, CONST_2, 2});                  // OpConstant 2
    op(43, {FLOAT, CONST_3, 3});                  // OpConstant 3
    op(59, {PTR_IN, FRAGCOORD, 0});               // OpVariable Input gl_FragCoord
    op(59, {PTR_OUT, FRAGDEPTH, 3});              // OpVariable Output gl_FragDepth

    // gl_FragCoord.z through an access chain: the real compiler shape.
    op(65, {PTR_IN_F, CHAIN, FRAGCOORD, CONST_2});   // OpAccessChain -> fc.z
    op(61, {FLOAT, LOADED, CHAIN});                   // OpLoad fc.z

    switch (shape) {
    case Shape::StoresZ:
        op(62, {FRAGDEPTH, LOADED});                     // store fc.z as it is
        break;
    case Shape::StoresRemap: {
        // fc.w combined with a push constant: the game's own depth remap.
        op(65, {PTR_IN_F, CHAIN_F, FRAGCOORD, CONST_3});   // OpAccessChain -> fc.w
        op(61, {FLOAT, PUSH, CHAIN_F});                    // OpLoad fc.w
        op(129, {FLOAT, VALUE, LOADED, PUSH});             // OpFAdd -> not fc.z
        op(62, {FRAGDEPTH, VALUE});                        // store the remap
        break;
    }
    case Shape::StoresLiteral:
        op(62, {FRAGDEPTH, CONST_1});                     // store a plain number
        break;
    }
    return w;
}

// The stored value id: the fixture stores LOADED for StoresZ, but the remap case
// overwrites VALUE, so the tests look at the predicate instead of an id.
static bool stores_rasterizer_z(Shape shape) {
    auto m = build_module(shape);
    return xbg_spirv_fragdepth_stores_rasterizer_z(m);
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
    // A module that stores exactly gl_FragCoord.z: the rasterizer would write
    // the same value, so dropping the store changes nothing.
    assert(stores_rasterizer_z(Shape::StoresZ));

    // A module that stores the game's own remapped depth (fc.z and fc.w with a
    // push constant) is NOT the rasterizer's z: dropping the store would replace
    // the game's depth with the interpolated z.
    assert(!stores_rasterizer_z(Shape::StoresRemap));

    // A module that stores a plain number is not the rasterizer's z either.
    assert(!stores_rasterizer_z(Shape::StoresLiteral));

    // The strip itself still removes the whole depth pattern.
    auto unused = build_module(Shape::StoresLiteral);
    assert(has_fragdepth_decoration(unused));
    assert(has_depth_replacing(unused));
    assert(stores_fragdepth(unused));
    auto stripped = unused;
    assert(xbg_terrain_drop_frag_depth(stripped));
    assert(!has_fragdepth_decoration(stripped));
    assert(!has_depth_replacing(stripped));
    assert(!stores_fragdepth(stripped));

    // After the strip there is no gl_FragDepth left, so nothing can be stored
    // into it any more.
    auto stripped_z = build_module(Shape::StoresZ);
    assert(xbg_terrain_drop_frag_depth(stripped_z));
    assert(!xbg_spirv_fragdepth_stores_rasterizer_z(stripped_z));

    // A module that is not SPIR-V is refused, and it stays as it was.
    auto broken = unused;
    broken[0] = 0;
    assert(!xbg_spirv_fragdepth_stores_rasterizer_z(broken));
    assert(!xbg_terrain_drop_frag_depth(broken));
    return 0;
}
