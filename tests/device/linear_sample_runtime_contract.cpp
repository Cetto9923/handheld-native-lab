#include "xbg_packed_shader_words.h"
#include "xbg_spirv_linear_sample.h"
#include <array>
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

using VkFormat = uint32_t;
using VkShaderModule = uint32_t;
using TypedStorageFormats = std::vector<std::pair<uint32_t, uint32_t>>;
constexpr uint32_t VK_FORMAT_R32_SFLOAT = 100,
                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT = 0x1000;
constexpr uint32_t VK_SAMPLE_COUNT_1_BIT = 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1;
constexpr uint32_t VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 1, XBG_SOURCE_TEXTURE = 7;
struct VkShaderModuleCreateInfo {
    uint32_t sType;
    size_t codeSize = 0;
    const uint32_t *pCode = nullptr;
};
struct samp_par {
    uint32_t minf = 0, magf = 0, ws = 0, wt = 0, wr = 0, cmp_mode = 0, cmp_func = 0;
    float aniso = 0, lod_min = 0, lod_max = 0;
};
struct xbg_shader_binding {
    uint32_t binding, type, count, bytes, source, slot, sampler_slot, flags;
};
struct view_t {};
struct tex_t {};
struct RegisteredTexture {
    tex_t texture;
    view_t view;
    bool has_view = false;
};
struct Pool {};
struct Image {
    uint32_t samples = 1, format = 100;
    uint32_t view_format(const view_t *, bool) {
        return format;
    }
} image;
struct Shader {
    std::string key = "0123456789abcdef", identity = key;
    uint32_t stage = 4;
    VkShaderModule module = 0;
    std::vector<uint32_t> linear_sample_candidates, module_words, inputs, input_classes,
        probe_words;
    XbgPackedShaderWords linear_sample_source;
    std::map<TypedStorageFormats, std::unique_ptr<Shader>> linear_sample_variants;
    std::vector<xbg_shader_binding> bindings;
    std::vector<uint8_t> c1;
    std::set<uint32_t> storage_writes, typed_storage_rewritten, filter_independent_bindings;
    std::map<uint32_t, uint32_t> storage_image_formats, storage_binding_info;
    std::vector<int> indirect_instance_fields;
    bool discards = false;
    int raster = 0;
};
struct {
    std::array<Shader *, 6> shader{};
    uint64_t tex[6][256]{}, samp[6][128]{};
    Pool texture_pool, sampler_pool;
} state;
const unsigned nvn_stage[6]{0, 2, 3, 4, 1, 5};
struct {
    int device = 0;
} cfg;
std::map<int, std::map<int, RegisteredTexture>> textures;
std::map<int, RegisteredTexture> legacy_textures;
std::map<int, std::map<int, samp_par>> samplers;
std::map<int, samp_par> legacy_samplers;
bool linear_supported = false;
unsigned creates = 0;
int pool_key(Pool *) {
    return 0;
}
Image &get_image(const tex_t *, bool upload) {
    assert(!upload);
    return image;
}
struct Features {
    uint32_t optimalTilingFeatures;
};
Features format_properties(VkFormat) {
    return {linear_supported ? 0x1000u : 0u};
}
void need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void xbg_logf(const char *, ...) {}
void xbg_fatalf(const char *, ...) {
    throw std::runtime_error("sampler lookup");
}
void validate_capabilities(const std::vector<uint32_t> &, uint32_t) {}
void xbg_spirv_indirect_instances(const std::vector<uint32_t> &, std::vector<int> &) {}
int vkCreateShaderModule(int, const VkShaderModuleCreateInfo *info, void *, VkShaderModule *out) {
    assert(info->codeSize && info->pCode);
    *out = ++creates;
    return 0;
}
namespace warm {
std::string identity;
void register_module(VkShaderModule, const std::string &value) {
    identity = value;
}
} // namespace warm
#define VK(call) need((call) == 0, "create module")
#include PRODUCTION_HEADER

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes.size() % 4 == 0);
    Shader base;
    base.module_words.resize(bytes.size() / 4);
    std::memcpy(base.module_words.data(), bytes.data(), bytes.size());
    base.linear_sample_candidates = xbg_spirv_linear_sample_candidates(base.module_words);
    assert(std::find(base.linear_sample_candidates.begin(), base.linear_sample_candidates.end(),
                     52) != base.linear_sample_candidates.end());
    base.bindings.push_back({52, 1, 1, 0, 7, 131, 0, 0x80000100});
    state.tex[1][131] = 1;
    state.samp[1][0] = uint64_t(2) << 20;
    state.shader[4] = &base;
    textures[0][1] = {};
    samp_par normal;
    normal.minf = 3;
    normal.magf = 1;
    normal.aniso = 1;
    normal.lod_max = 16;
    samplers[0][2] = normal;
    Shader *selected = linear_sample_variant(base);
    assert(selected != &base && creates == 1);
    assert(selected->identity == base.identity + ":lf1:52_100" &&
           warm::identity == selected->identity);
    assert(selected->filter_independent_bindings.count(52));
    assert(linear_sample_variant(base) == selected && creates == 1);
    {
        LinearSampleSelection selection(false);
        assert(state.shader[4] == selected);
    }
    assert(state.shader[4] == &base);
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        auto p = normal;
        if (scenario == 0)
            p.minf = 2;
        if (scenario == 1)
            p.magf = 0;
        if (scenario == 2)
            p.aniso = 2;
        if (scenario == 3)
            p.cmp_mode = 1;
        if (scenario == 4)
            p.lod_min = 1;
        if (scenario == 5)
            p.lod_max = -1;
        if (scenario == 6)
            p.minf = 7;
        samplers[0][2] = p;
        assert(linear_sample_variant(base) == &base && creates == 1);
    }
    samplers[0][2] = normal;
    linear_supported = true;
    assert(linear_sample_variant(base) == &base);
    linear_supported = false;
    image.format = 103;
    assert(linear_sample_variant(base) == &base);
    image.format = 100;
    image.samples = 4;
    assert(linear_sample_variant(base) == &base);
    image.samples = 1;
    base.bindings[0].count = 2;
    assert(linear_sample_variant(base) == &base);
    base.bindings[0].count = 1;
    base.bindings[0].source = 8;
    assert(linear_sample_variant(base) == &base);
    base.bindings[0].source = 7;
    state.tex[1][131] = 0;
    assert(linear_sample_variant(base) == &base);
    state.tex[1][131] = 1;
    assert(base.linear_sample_source.assign(base.module_words));
    base.module_words.clear();
    base.linear_sample_variants.clear();
    selected = linear_sample_variant(base);
    assert(selected != &base && creates == 2);
}
