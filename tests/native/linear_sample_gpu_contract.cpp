// Own float32 texture and shaders. Compare hardware bilinear sampling against
// the production SPIR-V rewrite using a nearest sampler on the same texels.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

static void check(VkResult result) {
    if (result != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(result));
}
struct Buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    void *data;
};
static std::vector<uint32_t> read(const char *path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("missing shader");
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes.size() % 4 == 0);
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}
int main(int argc, char **argv) {
    try {
        assert(argc == 3);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        VkInstance instance;
        check(vkCreateInstance(&ici, nullptr, &instance));
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        assert(count);
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        VkPhysicalDevice gpu = devices[0];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(gpu, &properties);
        VkFormatProperties format;
        vkGetPhysicalDeviceFormatProperties(gpu, VK_FORMAT_R32_SFLOAT, &format);
        assert(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, queues.data());
        uint32_t family = 0;
        while (family < families && !(queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT))
            ++family;
        assert(family < families);
        float priority = 1;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures supported;
        vkGetPhysicalDeviceFeatures(gpu, &supported);
        VkPhysicalDeviceFeatures enabled{};
        enabled.shaderImageGatherExtended = supported.shaderImageGatherExtended;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.pEnabledFeatures = &enabled;
        VkDevice device;
        check(vkCreateDevice(gpu, &dci, nullptr, &device));
        VkQueue queue;
        vkGetDeviceQueue(device, family, 0, &queue);
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
        auto memory_type = [&](uint32_t bits, VkMemoryPropertyFlags flags) {
            for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
                if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags)
                    return i;
            throw std::runtime_error("memory type");
        };
        auto buffer = [&](size_t bytes, VkBufferUsageFlags usage) {
            Buffer b{};
            VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            ci.size = bytes;
            ci.usage = usage;
            check(vkCreateBuffer(device, &ci, nullptr, &b.handle));
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device, b.handle, &requirements);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize = requirements.size;
            ai.memoryTypeIndex =
                memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkAllocateMemory(device, &ai, nullptr, &b.memory));
            check(vkBindBufferMemory(device, b.handle, b.memory, 0));
            check(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.data));
            return b;
        };
        std::vector<float> texels;
        std::vector<VkBufferImageCopy> copies;
        for (uint32_t level = 0; level < 3; ++level) {
            uint32_t width = 8u >> level;
            VkBufferImageCopy copy{};
            copy.bufferOffset = texels.size() * 4;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {width, width, 1};
            copies.push_back(copy);
            for (uint32_t i = 0; i < width * width; ++i)
                texels.push_back(.1234567f + float(i + level * 19) / 128.f);
        }
        Buffer staging = buffer(texels.size() * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
               output = buffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(staging.data, texels.data(), texels.size() * 4);
        VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        im.imageType = VK_IMAGE_TYPE_2D;
        im.format = VK_FORMAT_R32_SFLOAT;
        im.extent = {8, 8, 1};
        im.mipLevels = 3;
        im.arrayLayers = 1;
        im.samples = VK_SAMPLE_COUNT_1_BIT;
        im.tiling = VK_IMAGE_TILING_OPTIMAL;
        im.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImage image;
        check(vkCreateImage(device, &im, nullptr, &image));
        VkMemoryRequirements ir;
        vkGetImageMemoryRequirements(device, image, &ir);
        VkMemoryAllocateInfo ia{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ia.allocationSize = ir.size;
        ia.memoryTypeIndex = memory_type(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory image_memory;
        check(vkAllocateMemory(device, &ia, nullptr, &image_memory));
        check(vkBindImageMemory(device, image, image_memory, 0));
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = family;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool pool;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VkCommandBuffer command;
        check(vkAllocateCommandBuffers(device, &cai, &command));
        auto begin = [&] {
            check(vkResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(command, &info));
        };
        auto submit = [&] {
            check(vkEndCommandBuffer(command));
            VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            info.commandBufferCount = 1;
            info.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE));
            check(vkQueueWaitIdle(queue));
        };
        begin();
        VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        ib.image = image;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 3, 0, 1};
        ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
        vkCmdCopyBufferToImage(command, staging.handle, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               uint32_t(copies.size()), copies.data());
        ib.oldLayout = ib.newLayout;
        ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &ib);
        submit();
        VkDescriptorSetLayoutBinding bindings[2]{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        si.bindingCount = 2;
        si.pBindings = bindings;
        VkDescriptorSetLayout set_layout;
        check(vkCreateDescriptorSetLayout(device, &si, nullptr, &set_layout));
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &set_layout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &push;
        VkPipelineLayout layout;
        check(vkCreatePipelineLayout(device, &li, nullptr, &layout));
        VkPipeline pipelines[2];
        VkShaderModule modules[2];
        for (unsigned side = 0; side < 2; ++side) {
            auto code = read(argv[side + 1]);
            VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            mi.codeSize = code.size() * 4;
            mi.pCode = code.data();
            check(vkCreateShaderModule(device, &mi, nullptr, &modules[side]));
            VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ci.layout = layout;
            ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            ci.stage.module = modules[side];
            ci.stage.pName = "main";
            check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                           &pipelines[side]));
        }
        VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets = 1;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = sizes;
        VkDescriptorPool descriptor_pool;
        check(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptor_pool));
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descriptor_pool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &set_layout;
        VkDescriptorSet set;
        check(vkAllocateDescriptorSets(device, &dai, &set));
        float max_error = 0;
        unsigned checked = 0;
        for (unsigned base_level = 0; base_level < 2; ++base_level)
            for (unsigned mapping = 0; mapping < 2; ++mapping) {
                VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                vi.image = image;
                vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
                vi.format = im.format;
                vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base_level, 3 - base_level, 0, 1};
                if (mapping)
                    vi.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                                     VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_ZERO};
                VkImageView view;
                check(vkCreateImageView(device, &vi, nullptr, &view));
                for (VkSamplerAddressMode mode :
                     {VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER}) {
                    if (mapping && mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)
                        continue;
                    VkSampler samplers[2];
                    for (unsigned side = 0; side < 2; ++side) {
                        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                        info.minFilter = info.magFilter =
                            side ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
                        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                        info.addressModeU = info.addressModeV = info.addressModeW = mode;
                        info.maxLod = 2;
                        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                        check(vkCreateSampler(device, &info, nullptr, &samplers[side]));
                    }
                    for (float u :
                         {-.125f, 0.f, .03125f, .0625f, .09375f, .1875f, .5f, .96875f, 1.f, 1.125f})
                        for (float v : {-.125f, .0625f, .5f, .96875f, 1.125f}) {
                            const int width = 8 >> base_level;
                            const float x = u * width - .5f, y = v * width - .5f;
                            const int left = int(std::floor(x)), top = int(std::floor(y));
                            const float fraction_x = x - left, fraction_y = y - top;
                            std::array<float, 4> expected{};
                            float border_weight = 0;
                            for (int row = 0; row < 2; ++row)
                                for (int column = 0; column < 2; ++column) {
                                    int texel_x = left + column, texel_y = top + row;
                                    const float weight = (column ? fraction_x : 1 - fraction_x) *
                                                         (row ? fraction_y : 1 - fraction_y);
                                    if (mode == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
                                        (texel_x < 0 || texel_y < 0 || texel_x >= width ||
                                         texel_y >= width)) {
                                        border_weight += weight;
                                        continue;
                                    }
                                    if (mode == VK_SAMPLER_ADDRESS_MODE_REPEAT) {
                                        texel_x = (texel_x % width + width) % width;
                                        texel_y = (texel_y % width + width) % width;
                                    } else {
                                        texel_x = std::max(0, std::min(width - 1, texel_x));
                                        texel_y = std::max(0, std::min(width - 1, texel_y));
                                    }
                                    const float red =
                                        texels[copies[base_level].bufferOffset / sizeof(float) +
                                               texel_y * width + texel_x];
                                    const std::array<float, 4> texel =
                                        mapping ? std::array<float, 4>{red, red, 1, 0}
                                                : std::array<float, 4>{red, 0, 0, 1};
                                    for (unsigned component = 0; component < 4; ++component)
                                        expected[component] += weight * texel[component];
                                }
                            float values[2][4];
                            for (unsigned side = 0; side < 2; ++side) {
                                std::fill_n(static_cast<float *>(output.data), 4,
                                            std::numeric_limits<float>::quiet_NaN());
                                VkDescriptorImageInfo ii{samplers[side], view,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                                VkDescriptorBufferInfo bi{output.handle, 0, 16};
                                VkWriteDescriptorSet writes[2]{};
                                for (unsigned j = 0; j < 2; ++j) {
                                    writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                    writes[j].dstSet = set;
                                    writes[j].dstBinding = j;
                                    writes[j].descriptorCount = 1;
                                    writes[j].descriptorType = bindings[j].descriptorType;
                                }
                                writes[0].pImageInfo = &ii;
                                writes[1].pBufferInfo = &bi;
                                vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
                                begin();
                                vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                  pipelines[side]);
                                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                        layout, 0, 1, &set, 0, nullptr);
                                float uv[2]{u, v};
                                vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                                   8, uv);
                                vkCmdDispatch(command, 1, 1, 1);
                                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
                                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                                barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                     VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0,
                                                     nullptr, 0, nullptr);
                                submit();
                                std::memcpy(values[side], output.data, 16);
                                for (unsigned component = 0; component < 4; ++component) {
                                    float error =
                                        std::fabs(values[side][component] - expected[component]);
                                    // Vulkan permits missing border components to undergo normal
                                    // RGBA conversion. R32's missing alpha can therefore be one.
                                    // Both pipelines must still agree with each other below.
                                    if (component == 3 && !mapping && border_weight > 0)
                                        error = std::min(error, std::fabs(values[side][component] -
                                                                          expected[component] -
                                                                          border_weight));
                                    if (!std::isfinite(error) || error > 0.00001f) {
                                        std::fprintf(
                                            stderr,
                                            "CPU reference mismatch side=%u level=%u mapping=%u "
                                            "mode=%u uv=%g,%g c=%u expected=%g actual=%g\n",
                                            side, base_level, mapping, unsigned(mode), u, v,
                                            component, expected[component],
                                            values[side][component]);
                                        throw std::runtime_error(
                                            "independent bilinear reference mismatch");
                                    }
                                }
                            }
                            for (unsigned c = 0; c < 4; ++c) {
                                float error = std::fabs(values[0][c] - values[1][c]);
                                max_error = std::max(max_error, error);
                                if (!std::isfinite(error) || error > 0.00001f) {
                                    std::fprintf(stderr,
                                                 "mismatch level=%u mapping=%u mode=%u uv=%g,%g "
                                                 "c=%u hardware=%g lowered=%g\n",
                                                 base_level, mapping, unsigned(mode), u, v, c,
                                                 values[0][c], values[1][c]);
                                    throw std::runtime_error("bilinear mismatch");
                                }
                            }
                            ++checked;
                        }
                    for (auto sampler : samplers)
                        vkDestroySampler(device, sampler, nullptr);
                }
                vkDestroyImageView(device, view, nullptr);
            }
        std::printf("float32 bilinear GPU PASS device=%s samples=%u max_error=%.9g "
                    "original_format=R32_SFLOAT independent_cpu_reference=PASS\n",
                    properties.deviceName, checked, max_error);
        for (unsigned side = 0; side < 2; ++side) {
            vkDestroyPipeline(device, pipelines[side], nullptr);
            vkDestroyShaderModule(device, modules[side], nullptr);
        }
        vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyImage(device, image, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        for (auto b : {staging, output}) {
            vkUnmapMemory(device, b.memory);
            vkDestroyBuffer(device, b.handle, nullptr);
            vkFreeMemory(device, b.memory, nullptr);
        }
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "float32 bilinear GPU FAIL %s\n", error.what());
        return 1;
    }
}
