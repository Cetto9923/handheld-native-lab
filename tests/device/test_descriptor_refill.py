"""Exercise production descriptor refill and pool fallback with a counted driver."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ENGINE = Path(__file__).resolve().parents[2] / 'src/graphics/nvn_vulkan/engine'
FIXTURE = r'''
#include <vulkan/vulkan.h>
#include <cassert>
#include <map>
#include <set>
#include <vector>

template<class T> T handle(uintptr_t value) { return reinterpret_cast<T>(value); }

struct Slot {
    std::vector<VkDescriptorPool> pools;
    size_t pool_next = 0;
    std::map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>> spare_sets, used_sets;
} slot;

Slot &recording_slot() { return slot; }
struct { VkDevice device = handle<VkDevice>(1); } cfg;
VkPhysicalDeviceProperties properties{};
bool honor_descriptor_refill_experiment = false;
unsigned capacity = 0, created = 0, next_set = 0;
std::map<VkDescriptorPool, unsigned> remaining;
std::vector<unsigned> requests;

void check(VkResult result, const char *) { assert(result == VK_SUCCESS); }
#define VK(result) check(result, #result)

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice, const VkDescriptorPoolCreateInfo *info, const VkAllocationCallbacks *,
    VkDescriptorPool *pool) {
    assert(info->maxSets == 2048 && info->poolSizeCount > 0);
    *pool = handle<VkDescriptorPool>(++created);
    remaining[*pool] = capacity;
    return VK_SUCCESS;
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets) {
    requests.push_back(info->descriptorSetCount);
    auto &left = remaining.at(info->descriptorPool);
    if (left < info->descriptorSetCount) return VK_ERROR_OUT_OF_POOL_MEMORY;
    left -= info->descriptorSetCount;
    for (unsigned i = 0; i < info->descriptorSetCount; ++i) {
        assert(info->pSetLayouts[i] == handle<VkDescriptorSetLayout>(1));
        sets[i] = handle<VkDescriptorSet>(++next_set);
    }
    return VK_SUCCESS;
}

#include "allocation.h"

void exercise(bool enabled, uint32_t driver, unsigned batch) {
    slot = {}; requests.clear(); remaining.clear(); created = next_set = 0;
    capacity = batch + 1;
    honor_descriptor_refill_experiment = enabled;
    properties.vendorID = 0x5143;
    properties.deviceID = 0x43030b00;
    properties.driverVersion = driver;
    const auto layout = handle<VkDescriptorSetLayout>(1);
    std::set<VkDescriptorSet> handed_out;
    for (unsigned i = 0; i < batch + 2; ++i)
        assert(handed_out.insert(allocate_set(layout)).second);
    assert((requests == std::vector<unsigned>{batch, batch, 1, batch, 1, batch}));
    assert(created == 2 && slot.pool_next == 1);
    assert(slot.spare_sets.at(layout).size() == batch - 1);
    assert(slot.used_sets.at(layout).size() == handed_out.size());
}

int main() {
    exercise(false, 0x802fa02e, 16);
    exercise(true, 0x802fa02f, 16);
    exercise(true, 0x802fa02e, 4);
}
'''


class DescriptorRefill(unittest.TestCase):
    def test_batches_reuse_spares_and_preserve_pool_fallback(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('VkDescriptorSet allocate_set(VkDescriptorSetLayout layout){')
        end = source.index('// The proxy is an implementation-owned sampled resource', start)
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        if not ndk:
            ndk = sorted((Path.home() / 'Library/Android/sdk/ndk').glob('*'))[-1]
        ndk = Path(ndk)
        headers = next((ndk / 'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include/vulkan'))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'vulkan').symlink_to(headers, target_is_directory=True)
            (root / 'allocation.h').write_text(source[start:end])
            (root / 'fixture.cpp').write_text(FIXTURE)
            binary = root / 'fixture'
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-I', str(root), str(root / 'fixture.cpp'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
