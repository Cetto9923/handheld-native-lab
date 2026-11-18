"""Execute the production warm layout reader before its fake Vulkan calls."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'
text = source.read_text()
start = text.index('struct Reader{')
end = text.index('VkRenderPass make_pass(', start)

prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>
using VkShaderStageFlags = uint32_t;
using VkDescriptorType = int32_t;
using VkPipelineLayout = uint64_t;
using VkDescriptorSetLayout = uint64_t;
constexpr uint64_t VK_NULL_HANDLE = 0;
constexpr int VK_SUCCESS = 0;
constexpr int VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 1;
constexpr int VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 2;
[[maybe_unused]] constexpr VkDescriptorType VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT = 10;
[[maybe_unused]] constexpr uint32_t VK_SHADER_STAGE_FRAGMENT_BIT = 16;
[[maybe_unused]] constexpr uint32_t VK_SHADER_STAGE_COMPUTE_BIT = 32;
[[maybe_unused]] constexpr uint32_t VK_SHADER_STAGE_ALL_GRAPHICS = 31;
constexpr uint32_t VK_SHADER_STAGE_ALL = 0x7fffffff;
struct VkDescriptorSetLayoutBinding {
    uint32_t binding{};
    VkDescriptorType descriptorType{};
    uint32_t descriptorCount{};
    VkShaderStageFlags stageFlags{};
};
struct VkDescriptorSetLayoutCreateInfo {
    int sType;
    uint32_t flags{};
    uint32_t bindingCount{};
    const VkDescriptorSetLayoutBinding *pBindings{};
};
struct VkPushConstantRange { uint32_t stageFlags, offset, size; };
struct VkPipelineLayoutCreateInfo {
    int sType;
    uint32_t flags{};
    uint32_t setLayoutCount{};
    const VkDescriptorSetLayout *pSetLayouts{};
    uint32_t pushConstantRangeCount{};
    const VkPushConstantRange *pPushConstantRanges{};
};
struct { int device = 1; } cfg;
struct { struct { uint32_t maxPushConstantsSize = 128; } limits; } properties;
std::mutex mu;
std::map<std::string, VkPipelineLayout> layouts;
std::vector<VkDescriptorSetLayout> set_layouts;
unsigned set_calls, pipeline_calls;
int vkCreateDescriptorSetLayout(int, const VkDescriptorSetLayoutCreateInfo *, void *, VkDescriptorSetLayout *out) {
    ++set_calls;
    *out = 11;
    return VK_SUCCESS;
}
int vkCreatePipelineLayout(int, const VkPipelineLayoutCreateInfo *, void *, VkPipelineLayout *out) {
    ++pipeline_calls;
    *out = 22;
    return VK_SUCCESS;
}
void vkDestroyDescriptorSetLayout(int, VkDescriptorSetLayout, void *) {}
void vkDestroyPipelineLayout(int, VkPipelineLayout, void *) {}
'''

harness = r'''
using Binding = VkDescriptorSetLayoutBinding;
using Push = VkPushConstantRange;
void test(std::vector<Binding> bindings, std::vector<Push> pushes, bool accepted, uint32_t limit = 128) {
    layouts.clear();
    set_layouts.clear();
    set_calls = pipeline_calls = 0;
    properties.limits.maxPushConstantsSize = limit;
    std::vector<uint32_t> words{0, uint32_t(bindings.size())};
    for (auto b : bindings) {
        words.insert(words.end(), {b.binding, uint32_t(b.descriptorType), b.descriptorCount, b.stageFlags});
    }
    words.insert(words.end(), {0, 1, uint32_t(pushes.size())});
    for (auto p : pushes) words.insert(words.end(), {p.stageFlags, p.offset, p.size});
    const auto result = make_layout(words.data(), uint32_t(words.size()));
    assert((result != VK_NULL_HANDLE) == accepted);
    assert(set_calls == unsigned(accepted));
    assert(pipeline_calls == unsigned(accepted));
    if (accepted) {
        assert(make_layout(words.data(), uint32_t(words.size())) == result);
        assert(set_calls == 1 && pipeline_calls == 1);
    }
}
int main() {
    // Existing producer shapes, sparse/array bindings and core descriptor types.
    test({}, {}, true);
    test({{5, 6, 1, 32}, {11, 1, 1, 32}, {17, 1, 1, 32}, {23, 3, 1, 32}, {29, 3, 1, 32}, {35, 6, 1, 32}}, {}, true);
    for (int type = 0; type <= 10; ++type) test({{100, type, 128, type == 10 ? 16u : 63u}}, {}, true);
    test({{8, 6, 0, 0x80000000u}}, {}, true); // reserved binding with zero count
    test({{8, 6, 1, VK_SHADER_STAGE_ALL}}, {}, true);
    test({{8, 10, 1, 0}}, {}, true); // input attachment may be inaccessible
    test({{3, 6, 1, 1}, {3, 6, 1, 16}}, {}, false);
    test({{3, -1, 1, 32}}, {}, false);
    test({{3, 1000138000, 1, 32}}, {}, false); // inline-uniform feature is not enabled
    test({{3, 6, 1, 0x80000000u}}, {}, false);
    test({{3, 10, 1, 32}}, {}, false);
    test({}, {{32, 0, 4}}, true);
    test({}, {{1, 0, 128}, {16, 0, 128}}, true); // disjoint stages may overlap bytes
    test({}, {{32, 0, 128}}, true);
    test({}, {{32, 0, 132}}, false);
    test({}, {{32, 0, 132}}, true, 256); // actual device limit selects acceptance
    test({}, {{32, 128, 4}}, false);
    test({}, {{32, 4, 124}}, true);
    test({}, {{32, 4, 128}}, false);
    test({}, {{32, 1, 4}}, false);
    test({}, {{32, 0, 3}}, false);
    test({}, {{32, 0, 0}}, false);
    test({}, {{32, 4, 0xfffffffcu}}, false); // overflow-safe range check
    test({}, {{32, 0xfffffffcu, 4}}, false);
    test({}, {{32, 0, 4}, {32, 4, 4}}, false);
    test({}, {{0x80000000u, 0, 4}}, false);
}
'''

with tempfile.TemporaryDirectory(prefix='warm-layout-validation-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(prelude + text[start:end] + harness)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', '-fsanitize=address,undefined',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS actual warm layout validation, legal layout/cache reuse and malformed-input rejection (ASan/UBSan)')
