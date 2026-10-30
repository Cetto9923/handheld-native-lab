/* Read-only physical-device capability probe. No device, shaders, commands,
 * surfaces or application state are created. "Supported" is not "enabled". */
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static bool check(VkResult result, const char *operation) {
    if (result == VK_SUCCESS) return true;
    std::fprintf(stderr, "%s failed: %d\n", operation, int(result));
    return false;
}

int main() {
    uint32_t loader_version = VK_API_VERSION_1_0;
    auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (!enumerate_version || !check(enumerate_version(&loader_version), "vkEnumerateInstanceVersion") ||
        loader_version < VK_API_VERSION_1_1) {
        std::fprintf(stderr, "Vulkan 1.1 is required; loader=%u.%u.%u\n",
                     VK_VERSION_MAJOR(loader_version), VK_VERSION_MINOR(loader_version), VK_VERSION_PATCH(loader_version));
        return 1;
    }
    const uint32_t instance_version = std::min(loader_version, uint32_t(VK_API_VERSION_1_3));
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "ffrs-read-only-subgroup-probe";
    application.apiVersion = instance_version;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (!check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance")) return 1;
    uint32_t count = 0;
    int errors = 0;
    if (!check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices(count)") || !count) {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }
    std::vector<VkPhysicalDevice> physicals(count);
    if (!check(vkEnumeratePhysicalDevices(instance, &count, physicals.data()), "vkEnumeratePhysicalDevices")) {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }
    std::printf("loader_api=%u.%u.%u requested_instance_api=%u.%u.%u devices=%u\n",
                VK_VERSION_MAJOR(loader_version), VK_VERSION_MINOR(loader_version), VK_VERSION_PATCH(loader_version),
                VK_VERSION_MAJOR(instance_version), VK_VERSION_MINOR(instance_version), VK_VERSION_PATCH(instance_version), count);
    for (uint32_t index = 0; index < count; ++index) {
        VkPhysicalDevice physical = physicals[index];
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        std::printf("device=%u name=\"%s\" physical_api=%u.%u.%u vendor=%#x device_id=%#x driver=%#x\n",
                    index, properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
                    VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion),
                    properties.vendorID, properties.deviceID, properties.driverVersion);
        if (properties.apiVersion < VK_API_VERSION_1_1) {
            std::printf("device=%u Vulkan1.1_supported=0 subgroup_queries=NOT_RUN\n", index);
            continue;
        }
        uint32_t extension_count = 0;
        if (!check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr), "vkEnumerateDeviceExtensionProperties(count)")) {
            ++errors;
            continue;
        }
        std::vector<VkExtensionProperties> extensions(extension_count);
        if (!check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, extensions.data()), "vkEnumerateDeviceExtensionProperties")) {
            ++errors;
            continue;
        }
        bool subgroup_extension = false;
        for (const auto &extension : extensions)
            if (!std::strcmp(extension.extensionName, VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME)) subgroup_extension = true;
        const bool size_control_available = subgroup_extension ||
            (properties.apiVersion >= VK_API_VERSION_1_3 && instance_version >= VK_API_VERSION_1_3);
        VkPhysicalDeviceSubgroupSizeControlProperties size_properties{};
        size_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
        VkPhysicalDeviceSubgroupProperties subgroup{};
        subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        subgroup.pNext = size_control_available ? &size_properties : nullptr;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &subgroup;
        vkGetPhysicalDeviceProperties2(physical, &properties2);

        VkPhysicalDeviceSubgroupSizeControlFeatures size_features{};
        size_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES;
        const bool has_vulkan12 = properties.apiVersion >= VK_API_VERSION_1_2 &&
            instance_version >= VK_API_VERSION_1_2;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        size_features.pNext = has_vulkan12 ? &features12 : nullptr;
        VkPhysicalDeviceShaderDrawParametersFeatures draw_features{};
        draw_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        draw_features.pNext = size_control_available ? static_cast<void *>(&size_features) :
            (has_vulkan12 ? static_cast<void *>(&features12) : nullptr);
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &draw_features;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        const bool require32_supported = size_control_available && size_features.subgroupSizeControl &&
            size_properties.minSubgroupSize <= 32 && size_properties.maxSubgroupSize >= 32 &&
            (size_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT);
        std::printf("device=%u subgroup_size=%u supported_stages=%#x supported_operations=%#x quad_all_stages=%u\n",
                    index, subgroup.subgroupSize, subgroup.supportedStages, subgroup.supportedOperations, subgroup.quadOperationsInAllStages);
        std::printf("device=%u size_control_extension=%u queried=%u min=%u max=%u max_compute_workgroup_subgroups=%u required_stages=%#x\n",
                    index, unsigned(subgroup_extension), unsigned(size_control_available), size_properties.minSubgroupSize,
                    size_properties.maxSubgroupSize, size_properties.maxComputeWorkgroupSubgroups, size_properties.requiredSubgroupSizeStages);
        std::printf("device=%u supported_subgroupSizeControl=%u supported_computeFullSubgroups=%u supported_shaderDrawParameters=%u require_compute32_supported=%u logical_device_created=0\n",
                    index, size_features.subgroupSizeControl, size_features.computeFullSubgroups,
                    draw_features.shaderDrawParameters, unsigned(require32_supported));
        std::printf("device=%u supported_geometryShader=%u supported_tessellationShader=%u supported_samplerAnisotropy=%u supported_independentBlend=%u supported_shaderStorageImageReadWithoutFormat=%u supported_shaderStorageImageWriteWithoutFormat=%u\n",
                    index, features.features.geometryShader, features.features.tessellationShader,
                    features.features.samplerAnisotropy, features.features.independentBlend,
                    features.features.shaderStorageImageReadWithoutFormat, features.features.shaderStorageImageWriteWithoutFormat);
        std::printf("device=%u Vulkan12_queried=%u supported_shaderOutputLayer=%u supported_shaderOutputViewportIndex=%u supported_descriptorIndexing=%u supported_sampledImageNonUniform=%u supported_uniformTexelNonUniform=%u\n",
                    index,unsigned(has_vulkan12),features12.shaderOutputLayer,features12.shaderOutputViewportIndex,
                    features12.descriptorIndexing,features12.shaderSampledImageArrayNonUniformIndexing,
                    features12.shaderUniformTexelBufferArrayNonUniformIndexing);
        const VkFormat texture_formats[]={VK_FORMAT_R16_UNORM,VK_FORMAT_R16G16_UNORM,
            VK_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R32G32_SFLOAT,
            VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM};
        for(VkFormat format:texture_formats){VkFormatProperties format_properties{};
            vkGetPhysicalDeviceFormatProperties(physical,format,&format_properties);
            std::printf("device=%u texture_format=%u optimal=%#x sampled_linear=%u\n",index,unsigned(format),
                format_properties.optimalTilingFeatures,unsigned(bool(format_properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)));}
    }
    vkDestroyInstance(instance, nullptr);
    std::printf("vulkan_subgroup_probe errors=%d capability_only=1\n", errors);
    return errors != 0;
}
