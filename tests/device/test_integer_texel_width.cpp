// Standalone formatless integer storage-texel contract. No game inputs.
// Usage: test_integer_texel_width integer_texel_width.spv
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#define VK_CHECK(call) do { \
    VkResult result = (call); \
    if (result != VK_SUCCESS) { \
        std::fprintf(stderr, "%s failed result=%d line=%d\n", #call, result, __LINE__); \
        std::exit(2); \
    } \
} while (0)

namespace {
constexpr size_t buffer_bytes = 128;
constexpr VkDeviceSize view_bytes = 64;
constexpr uint8_t sentinel = 0xa5;
constexpr std::array<uint32_t, 8> values{{2, 9, 17, 0, 511, 65535, 4, 12}};

bool shader_contract(const std::vector<uint32_t> &words) {
    if (words.size() < 5 || words[0] != 0x07230203u) return false;
    uint32_t uint_type = 0;
    bool write_without_format = false;
    unsigned image_types = 0;
    unsigned image_writes = 0;
    for (size_t i = 5; i < words.size();) {
        const uint32_t count = words[i] >> 16;
        const uint32_t op = words[i] & 65535;
        if (!count || count > words.size() - i) return false;
        if (op == 17 && count == 2 && words[i + 1] == 56) write_without_format = true;
        if (op == 21 && count == 4 && words[i + 2] == 32 && words[i + 3] == 0)
            uint_type = words[i + 1];
        if (op == 25) {
            if (count != 9 || !uint_type || words[i + 2] != uint_type ||
                words[i + 3] != 5 || words[i + 7] != 2 || words[i + 8] != 0)
                return false;
            ++image_types;
        }
        if (op == 99) ++image_writes;
        i += count;
    }
    return write_without_format && image_types == 1 && image_writes == 1;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s integer_texel_width.spv\n", argv[0]);
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() < 20 || bytes.size() % 4) return 2;
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    if (!shader_contract(words)) {
        std::fprintf(stderr, "Shader must have UInt32 Buffer Unknown storage type and formatless writes\n");
        return 2;
    }

    uint32_t loader_version = VK_API_VERSION_1_0;
    auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerate_version) VK_CHECK(enumerate_version(&loader_version));
    if (loader_version < VK_API_VERSION_1_1) return 3;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "FFRS-integer-texel-width-contract";
    app.apiVersion = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) return 3;
    std::vector<VkPhysicalDevice> physical_devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()));
    VkPhysicalDevice physical = physical_devices[0];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    VkPhysicalDeviceFeatures available;
    vkGetPhysicalDeviceFeatures(physical, &available);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
    bool flags2_extension = false;
    for (const auto &extension : extensions)
        if (!std::strcmp(extension.extensionName, VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME))
            flags2_extension = true;
    const bool flags2_core = app.apiVersion >= VK_API_VERSION_1_3 && properties.apiVersion >= VK_API_VERSION_1_3;
    const bool flags2 = flags2_core || flags2_extension;
    std::printf("GPU=%s api=%u.%u driver=%u formatFeatureFlags2=%u extension=%u writeWithoutFormatSupported=%u\n",
        properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
        VK_API_VERSION_MINOR(properties.apiVersion), properties.driverVersion,
        unsigned(flags2), unsigned(flags2_extension), available.shaderStorageImageWriteWithoutFormat);

    const std::array<VkFormat, 2> formats{{VK_FORMAT_R16_UINT, VK_FORMAT_R32_UINT}};
    bool supported = available.shaderStorageImageWriteWithoutFormat;
    for (VkFormat format : formats) {
        VkFormatProperties3 extended{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
        VkFormatProperties2 queried{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        if (flags2) queried.pNext = &extended;
        vkGetPhysicalDeviceFormatProperties2(physical, format, &queried);
        const bool storage = queried.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
        const bool formatless = flags2 ? bool(extended.bufferFeatures & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT) : bool(available.shaderStorageImageWriteWithoutFormat);
        std::printf("format=%d bufferFeatures=0x%x bufferFeatures2=0x%llx storageTexel=%u formatlessWrite=%u supportSource=%s\n",
            int(format), queried.formatProperties.bufferFeatures,
            static_cast<unsigned long long>(extended.bufferFeatures), unsigned(storage),
            unsigned(formatless), flags2 ? "bufferFeatures2" : "core-feature-listed-format");
        supported = supported && storage && formatless;
    }
    if (!supported) {
        std::printf("GPU_INTEGER_TEXEL_WIDTH_UNSUPPORTED\n");
        vkDestroyInstance(instance, nullptr);
        return 3;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    if (family == count) return 3;
    float priority = 1;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    VkPhysicalDeviceFeatures enabled{};
    enabled.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    const char *extension_name = VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &enabled;
    if (flags2_extension && !flags2_core) {
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = &extension_name;
    }
    VkDevice device;
    VK_CHECK(vkCreateDevice(physical, &device_info, nullptr, &device));
    std::printf("writeWithoutFormatEnabled=%u\n", enabled.shaderStorageImageWriteWithoutFormat);
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = buffer_bytes;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    VkBuffer buffer;
    VK_CHECK(vkCreateBuffer(device, &buffer_info, nullptr, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t memory_type = 0;
    const VkMemoryPropertyFlags needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (memory_type < memory_properties.memoryTypeCount &&
        (!(requirements.memoryTypeBits & (1u << memory_type)) ||
         (memory_properties.memoryTypes[memory_type].propertyFlags & needed) != needed)) ++memory_type;
    if (memory_type == memory_properties.memoryTypeCount) return 3;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    VkDeviceMemory memory;
    VK_CHECK(vkAllocateMemory(device, &allocation, nullptr, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    void *mapped;
    VK_CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    std::array<VkBufferView, 2> views{};
    for (size_t mode = 0; mode < formats.size(); ++mode) {
        VkBufferViewCreateInfo view_info{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
        view_info.buffer = buffer;
        view_info.format = formats[mode];
        view_info.range = view_bytes;
        VK_CHECK(vkCreateBufferView(device, &view_info, nullptr, &views[mode]));
    }

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout));
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &layout;
    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout));
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = bytes.size();
    module_info.pCode = words.data();
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &module_info, nullptr, &module));
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.layout = pipeline_layout;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = module;
    pipeline_info.stage.pName = "main";
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool));
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set_info.descriptorPool = pool;
    set_info.descriptorSetCount = 1;
    set_info.pSetLayouts = &layout;
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &set_info, &set));
    VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    command_pool_info.queueFamilyIndex = family;
    command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = command_pool;
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    VkCommandBuffer command;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &fence));

    bool all_ok = true;
    for (size_t mode = 0; mode < formats.size(); ++mode) {
        std::memset(mapped, sentinel, buffer_bytes);
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        write.pTexelBufferView = &views[mode];
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        VK_CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(command, &begin));
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &before, 0, nullptr, 0, nullptr);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(command, 1, 1, 1);
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &after, 0, nullptr, 0, nullptr);
        VK_CHECK(vkEndCommandBuffer(command));
        VK_CHECK(vkResetFences(device, 1, &fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));

        const size_t width = mode == 0 ? 2 : 4;
        std::array<uint8_t, buffer_bytes> expected;
        expected.fill(sentinel);
        for (size_t i = 0; i < values.size(); ++i) {
            if (width == 2) {
                const uint16_t value = static_cast<uint16_t>(values[i]);
                std::memcpy(expected.data() + i * width, &value, width);
            } else std::memcpy(expected.data() + i * width, &values[i], width);
        }
        const auto *actual = static_cast<const uint8_t *>(mapped);
        bool layout_ok = !std::memcmp(actual, expected.data(), values.size() * width);
        bool tail_ok = !std::memcmp(actual + values.size() * width,
            expected.data() + values.size() * width, buffer_bytes - values.size() * width);
        std::printf("format=%d viewBytes=%llu texelsWritten=%zu bytesWritten=%zu layout=%s tailSentinels=%s bytes=",
            int(formats[mode]), static_cast<unsigned long long>(view_bytes), values.size(),
            values.size() * width, layout_ok ? "PASS" : "FAIL", tail_ok ? "PASS" : "FAIL");
        for (size_t i = 0; i < 32; ++i) std::printf("%02x", actual[i]);
        std::printf(" r16Interpretation=");
        for (size_t i = 0; i < 16; ++i) {
            uint16_t value;
            std::memcpy(&value, actual + i * 2, 2);
            std::printf("%s%u", i ? "," : "", unsigned(value));
        }
        std::printf("\n");
        all_ok = all_ok && layout_ok && tail_ok;
    }
    std::printf("GPU_INTEGER_TEXEL_WIDTH_%s cases=2\n", all_ok ? "PASS" : "FAIL");
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    for (VkBufferView view : views) vkDestroyBufferView(device, view, nullptr);
    vkUnmapMemory(device, memory);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return all_ok ? 0 : 4;
}
