#include "probe_vulkan.h"
#include "probe_types.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#endif

namespace ffrs {

#if defined(__ANDROID__)

static std::string FormatVkVersion(uint32_t ver) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u",
                  VK_VERSION_MAJOR(ver),
                  VK_VERSION_MINOR(ver),
                  VK_VERSION_PATCH(ver));
    return buf;
}

static const char* VkDeviceTypeToString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "DISCRETE_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "VIRTUAL_GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "OTHER";
    }
}

static const char* VkVendorIdToString(uint32_t vendorId) {
    switch (vendorId) {
        case 0x13B5: return "ARM";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x5143: return "Qualcomm";
        case 0x1010: return "Imagination";
        case 0x14E4: return "Broadcom";
        default:     return "Unknown";
    }
}

static void AddBoolFlag(BoolFlags& flags, const char* name, bool val) {
    flags.names.emplace_back(name);
    flags.values.push_back(val);
}

static void AddUintFlag(UintFlags& flags, const char* name, uint64_t val) {
    flags.names.emplace_back(name);
    flags.values.push_back(val);
}

static std::vector<std::string> DecodeOptimalTilingFeatures(VkFormatFeatureFlags f) {
    std::vector<std::string> caps;
    if (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) caps.emplace_back("SAMPLED_IMAGE");
    if (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) caps.emplace_back("STORAGE_IMAGE");
    if (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT) caps.emplace_back("STORAGE_IMAGE_ATOMIC");
    if (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) caps.emplace_back("COLOR_ATTACHMENT");
    if (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) caps.emplace_back("COLOR_ATTACHMENT_BLEND");
    if (f & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) caps.emplace_back("DEPTH_STENCIL_ATTACHMENT");
    if (f & VK_FORMAT_FEATURE_BLIT_SRC_BIT) caps.emplace_back("BLIT_SRC");
    if (f & VK_FORMAT_FEATURE_BLIT_DST_BIT) caps.emplace_back("BLIT_DST");
    if (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) caps.emplace_back("FILTER_LINEAR");
    return caps;
}

void CollectVulkan(const ProbeContext& /*ctx*/, VulkanReport& out) {
    // 1. Check vkEnumerateInstanceVersion
    uint32_t instVer = VK_API_VERSION_1_0;
    auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerate_version != nullptr) {
        if (enumerate_version(&instVer) == VK_SUCCESS) {
            out.instance_version_query_supported = true;
        }
    }
    out.instance_api_version_raw = instVer;
    out.instance_api_version = FormatVkVersion(instVer);

    // 2. Create Vulkan Instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "probe_rg477m";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "FFRS Native";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = (instVer >= VK_API_VERSION_1_3) ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = vkCreateInstance(&instInfo, nullptr, &instance);
    if (res != VK_SUCCESS) {
        // Fallback to Vulkan 1.0 if 1.3 / 1.1 failed
        appInfo.apiVersion = VK_API_VERSION_1_0;
        res = vkCreateInstance(&instInfo, nullptr, &instance);
    }

    if (res != VK_SUCCESS) {
        out.status = "error_create_instance";
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "vkCreateInstance returned %d", res);
        out.error = errBuf;
        return;
    }

    // 3. Enumerate Physical Devices
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    out.physical_device_count = static_cast<int>(devCount);

    if (devCount == 0) {
        out.status = "no_physical_devices";
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> physDevices(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, physDevices.data());

    // Function pointers for Vulkan 1.1+ features
    PFN_vkGetPhysicalDeviceProperties2 pfnGetProps2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
    if (!pfnGetProps2) {
        pfnGetProps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2KHR"));
    }

    PFN_vkGetPhysicalDeviceFeatures2 pfnGetFeat2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    if (!pfnGetFeat2) {
        pfnGetFeat2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2KHR"));
    }

    // 4. Inspect each physical device
    for (uint32_t i = 0; i < devCount; i++) {
        VkPhysicalDevice pdev = physDevices[i];
        VulkanPhysicalDeviceReport rep;
        rep.index = static_cast<int>(i);

        // Core properties
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pdev, &props);
        rep.name = props.deviceName;
        rep.type_raw = props.deviceType;
        rep.type = VkDeviceTypeToString(props.deviceType);
        rep.vendor_id = props.vendorID;
        rep.vendor_name = VkVendorIdToString(props.vendorID);
        rep.device_id = props.deviceID;
        rep.api_version_raw = props.apiVersion;
        rep.api_version = FormatVkVersion(props.apiVersion);
        rep.driver_version_raw = props.driverVersion;
        rep.driver_version = FormatVkVersion(props.driverVersion);

        // Extended driver properties
        if (pfnGetProps2 != nullptr) {
            VkPhysicalDeviceDriverProperties driverProps{};
            driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &driverProps;

            pfnGetProps2(pdev, &props2);

            rep.driver_name = driverProps.driverName;
            rep.driver_info = driverProps.driverInfo;
            char confBuf[64];
            std::snprintf(confBuf, sizeof(confBuf), "%u.%u.%u.%u",
                          driverProps.conformanceVersion.major,
                          driverProps.conformanceVersion.minor,
                          driverProps.conformanceVersion.subminor,
                          driverProps.conformanceVersion.patch);
            rep.conformance_version = confBuf;
        }

        // Enumerate extensions
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(pdev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(pdev, nullptr, &extCount, exts.data());
        for (const auto& ext : exts) {
            rep.extensions.emplace_back(ext.extensionName);
            if (std::strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
                rep.supports_portability_subset = true;
            }
        }

        // Core features
        VkPhysicalDeviceFeatures coreFeat;
        vkGetPhysicalDeviceFeatures(pdev, &coreFeat);
        AddBoolFlag(rep.features_core, "robustBufferAccess", coreFeat.robustBufferAccess);
        AddBoolFlag(rep.features_core, "fullDrawIndexUint32", coreFeat.fullDrawIndexUint32);
        AddBoolFlag(rep.features_core, "imageCubeArray", coreFeat.imageCubeArray);
        AddBoolFlag(rep.features_core, "independentBlend", coreFeat.independentBlend);
        AddBoolFlag(rep.features_core, "geometryShader", coreFeat.geometryShader);
        AddBoolFlag(rep.features_core, "tessellationShader", coreFeat.tessellationShader);
        AddBoolFlag(rep.features_core, "sampleRateShading", coreFeat.sampleRateShading);
        AddBoolFlag(rep.features_core, "dualSrcBlend", coreFeat.dualSrcBlend);
        AddBoolFlag(rep.features_core, "logicOp", coreFeat.logicOp);
        AddBoolFlag(rep.features_core, "multiDrawIndirect", coreFeat.multiDrawIndirect);
        AddBoolFlag(rep.features_core, "depthClamp", coreFeat.depthClamp);
        AddBoolFlag(rep.features_core, "depthBiasClamp", coreFeat.depthBiasClamp);
        AddBoolFlag(rep.features_core, "fillModeNonSolid", coreFeat.fillModeNonSolid);
        AddBoolFlag(rep.features_core, "samplerAnisotropy", coreFeat.samplerAnisotropy);
        AddBoolFlag(rep.features_core, "textureCompressionETC2", coreFeat.textureCompressionETC2);
        AddBoolFlag(rep.features_core, "textureCompressionASTC_LDR", coreFeat.textureCompressionASTC_LDR);
        AddBoolFlag(rep.features_core, "textureCompressionBC", coreFeat.textureCompressionBC);
        AddBoolFlag(rep.features_core, "shaderImageGatherExtended", coreFeat.shaderImageGatherExtended);
        AddBoolFlag(rep.features_core, "shaderStorageImageExtendedFormats", coreFeat.shaderStorageImageExtendedFormats);
        AddBoolFlag(rep.features_core, "shaderUniformBufferArrayDynamicIndexing", coreFeat.shaderUniformBufferArrayDynamicIndexing);
        AddBoolFlag(rep.features_core, "shaderSampledImageArrayDynamicIndexing", coreFeat.shaderSampledImageArrayDynamicIndexing);
        AddBoolFlag(rep.features_core, "shaderStorageBufferArrayDynamicIndexing", coreFeat.shaderStorageBufferArrayDynamicIndexing);
        AddBoolFlag(rep.features_core, "shaderStorageImageArrayDynamicIndexing", coreFeat.shaderStorageImageArrayDynamicIndexing);
        AddBoolFlag(rep.features_core, "shaderInt64", coreFeat.shaderInt64);
        AddBoolFlag(rep.features_core, "shaderInt16", coreFeat.shaderInt16);

        // Vulkan 1.1, 1.2, 1.3 & Descriptor Indexing features
        if (pfnGetFeat2 != nullptr) {
            VkPhysicalDeviceVulkan11Features f11{};
            f11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

            VkPhysicalDeviceVulkan12Features f12{};
            f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

            VkPhysicalDeviceVulkan13Features f13{};
            f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

            VkPhysicalDeviceDescriptorIndexingFeatures fDescIdx{};
            fDescIdx.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

            VkPhysicalDeviceFeatures2 feat2{};
            feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            feat2.pNext = &f11;
            f11.pNext = &f12;
            f12.pNext = &f13;
            f13.pNext = &fDescIdx;

            pfnGetFeat2(pdev, &feat2);

            // Vulkan 1.1 features
            AddBoolFlag(rep.features_vulkan11, "storageBuffer16BitAccess", f11.storageBuffer16BitAccess);
            AddBoolFlag(rep.features_vulkan11, "uniformAndStorageBuffer16BitAccess", f11.uniformAndStorageBuffer16BitAccess);
            AddBoolFlag(rep.features_vulkan11, "storagePushConstant16", f11.storagePushConstant16);
            AddBoolFlag(rep.features_vulkan11, "multiview", f11.multiview);
            AddBoolFlag(rep.features_vulkan11, "variablePointersStorageBuffer", f11.variablePointersStorageBuffer);
            AddBoolFlag(rep.features_vulkan11, "variablePointers", f11.variablePointers);
            AddBoolFlag(rep.features_vulkan11, "shaderDrawParameters", f11.shaderDrawParameters);

            // Vulkan 1.2 features
            AddBoolFlag(rep.features_vulkan12, "samplerMirrorClampToEdge", f12.samplerMirrorClampToEdge);
            AddBoolFlag(rep.features_vulkan12, "drawIndirectCount", f12.drawIndirectCount);
            AddBoolFlag(rep.features_vulkan12, "storageBuffer8BitAccess", f12.storageBuffer8BitAccess);
            AddBoolFlag(rep.features_vulkan12, "uniformAndStorageBuffer8BitAccess", f12.uniformAndStorageBuffer8BitAccess);
            AddBoolFlag(rep.features_vulkan12, "shaderBufferInt64Atomics", f12.shaderBufferInt64Atomics);
            AddBoolFlag(rep.features_vulkan12, "shaderSharedInt64Atomics", f12.shaderSharedInt64Atomics);
            AddBoolFlag(rep.features_vulkan12, "shaderFloat16", f12.shaderFloat16);
            AddBoolFlag(rep.features_vulkan12, "shaderInt8", f12.shaderInt8);
            AddBoolFlag(rep.features_vulkan12, "descriptorIndexing", f12.descriptorIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderInputAttachmentArrayDynamicIndexing", f12.shaderInputAttachmentArrayDynamicIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderUniformTexelBufferArrayDynamicIndexing", f12.shaderUniformTexelBufferArrayDynamicIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderStorageTexelBufferArrayDynamicIndexing", f12.shaderStorageTexelBufferArrayDynamicIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderUniformBufferArrayNonUniformIndexing", f12.shaderUniformBufferArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderSampledImageArrayNonUniformIndexing", f12.shaderSampledImageArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderStorageBufferArrayNonUniformIndexing", f12.shaderStorageBufferArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderStorageImageArrayNonUniformIndexing", f12.shaderStorageImageArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderInputAttachmentArrayNonUniformIndexing", f12.shaderInputAttachmentArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderUniformTexelBufferArrayNonUniformIndexing", f12.shaderUniformTexelBufferArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "shaderStorageTexelBufferArrayNonUniformIndexing", f12.shaderStorageTexelBufferArrayNonUniformIndexing);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingUniformBufferUpdateAfterBind", f12.descriptorBindingUniformBufferUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingSampledImageUpdateAfterBind", f12.descriptorBindingSampledImageUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingStorageImageUpdateAfterBind", f12.descriptorBindingStorageImageUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingStorageBufferUpdateAfterBind", f12.descriptorBindingStorageBufferUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingUniformTexelBufferUpdateAfterBind", f12.descriptorBindingUniformTexelBufferUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingStorageTexelBufferUpdateAfterBind", f12.descriptorBindingStorageTexelBufferUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingUpdateUnusedWhilePending", f12.descriptorBindingUpdateUnusedWhilePending);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingPartiallyBound", f12.descriptorBindingPartiallyBound);
            AddBoolFlag(rep.features_vulkan12, "descriptorBindingVariableDescriptorCount", f12.descriptorBindingVariableDescriptorCount);
            AddBoolFlag(rep.features_vulkan12, "runtimeDescriptorArray", f12.runtimeDescriptorArray);
            AddBoolFlag(rep.features_vulkan12, "samplerFilterMinmax", f12.samplerFilterMinmax);
            AddBoolFlag(rep.features_vulkan12, "scalarBlockLayout", f12.scalarBlockLayout);
            AddBoolFlag(rep.features_vulkan12, "imagelessFramebuffer", f12.imagelessFramebuffer);
            AddBoolFlag(rep.features_vulkan12, "uniformBufferStandardLayout", f12.uniformBufferStandardLayout);
            AddBoolFlag(rep.features_vulkan12, "timelineSemaphore", f12.timelineSemaphore);
            AddBoolFlag(rep.features_vulkan12, "bufferDeviceAddress", f12.bufferDeviceAddress);
            AddBoolFlag(rep.features_vulkan12, "vulkanMemoryModel", f12.vulkanMemoryModel);

            // Vulkan 1.3 features
            AddBoolFlag(rep.features_vulkan13, "robustImageAccess", f13.robustImageAccess);
            AddBoolFlag(rep.features_vulkan13, "inlineUniformBlock", f13.inlineUniformBlock);
            AddBoolFlag(rep.features_vulkan13, "descriptorBindingInlineUniformBlockUpdateAfterBind", f13.descriptorBindingInlineUniformBlockUpdateAfterBind);
            AddBoolFlag(rep.features_vulkan13, "pipelineCreationCacheControl", f13.pipelineCreationCacheControl);
            AddBoolFlag(rep.features_vulkan13, "privateData", f13.privateData);
            AddBoolFlag(rep.features_vulkan13, "shaderDemoteToHelperInvocation", f13.shaderDemoteToHelperInvocation);
            AddBoolFlag(rep.features_vulkan13, "shaderTerminateInvocation", f13.shaderTerminateInvocation);
            AddBoolFlag(rep.features_vulkan13, "subgroupSizeControl", f13.subgroupSizeControl);
            AddBoolFlag(rep.features_vulkan13, "synchronization2", f13.synchronization2);
            AddBoolFlag(rep.features_vulkan13, "textureCompressionASTC_HDR", f13.textureCompressionASTC_HDR);
            AddBoolFlag(rep.features_vulkan13, "dynamicRendering", f13.dynamicRendering);
            AddBoolFlag(rep.features_vulkan13, "maintenance4", f13.maintenance4);

            // Bindless / descriptor indexing focus
            AddBoolFlag(rep.descriptor_indexing, "descriptorBindingPartiallyBound", fDescIdx.descriptorBindingPartiallyBound);
            AddBoolFlag(rep.descriptor_indexing, "runtimeDescriptorArray", fDescIdx.runtimeDescriptorArray);
            AddBoolFlag(rep.descriptor_indexing, "shaderSampledImageArrayNonUniformIndexing", fDescIdx.shaderSampledImageArrayNonUniformIndexing);
            AddBoolFlag(rep.descriptor_indexing, "shaderStorageBufferArrayNonUniformIndexing", fDescIdx.shaderStorageBufferArrayNonUniformIndexing);
            AddBoolFlag(rep.descriptor_indexing, "descriptorBindingSampledImageUpdateAfterBind", fDescIdx.descriptorBindingSampledImageUpdateAfterBind);
            AddBoolFlag(rep.descriptor_indexing, "descriptorBindingStorageBufferUpdateAfterBind", fDescIdx.descriptorBindingStorageBufferUpdateAfterBind);
            AddBoolFlag(rep.descriptor_indexing, "descriptorBindingUpdateUnusedWhilePending", fDescIdx.descriptorBindingUpdateUnusedWhilePending);
            AddBoolFlag(rep.descriptor_indexing, "descriptorBindingVariableDescriptorCount", fDescIdx.descriptorBindingVariableDescriptorCount);
        }

        // Limits
        const auto& lim = props.limits;
        AddUintFlag(rep.limits, "maxImageDimension1D", lim.maxImageDimension1D);
        AddUintFlag(rep.limits, "maxImageDimension2D", lim.maxImageDimension2D);
        AddUintFlag(rep.limits, "maxImageDimension3D", lim.maxImageDimension3D);
        AddUintFlag(rep.limits, "maxImageDimensionCube", lim.maxImageDimensionCube);
        AddUintFlag(rep.limits, "maxImageArrayLayers", lim.maxImageArrayLayers);
        AddUintFlag(rep.limits, "maxTexelBufferElements", lim.maxTexelBufferElements);
        AddUintFlag(rep.limits, "maxUniformBufferRange", lim.maxUniformBufferRange);
        AddUintFlag(rep.limits, "maxStorageBufferRange", lim.maxStorageBufferRange);
        AddUintFlag(rep.limits, "maxPushConstantsSize", lim.maxPushConstantsSize);
        AddUintFlag(rep.limits, "maxMemoryAllocationCount", lim.maxMemoryAllocationCount);
        AddUintFlag(rep.limits, "maxSamplerAllocationCount", lim.maxSamplerAllocationCount);
        AddUintFlag(rep.limits, "bufferImageGranularity", lim.bufferImageGranularity);
        AddUintFlag(rep.limits, "sparseAddressSpaceSize", lim.sparseAddressSpaceSize);
        AddUintFlag(rep.limits, "maxBoundDescriptorSets", lim.maxBoundDescriptorSets);
        AddUintFlag(rep.limits, "maxPerStageDescriptorSamplers", lim.maxPerStageDescriptorSamplers);
        AddUintFlag(rep.limits, "maxPerStageDescriptorUniformBuffers", lim.maxPerStageDescriptorUniformBuffers);
        AddUintFlag(rep.limits, "maxPerStageDescriptorStorageBuffers", lim.maxPerStageDescriptorStorageBuffers);
        AddUintFlag(rep.limits, "maxPerStageDescriptorSampledImages", lim.maxPerStageDescriptorSampledImages);
        AddUintFlag(rep.limits, "maxPerStageDescriptorStorageImages", lim.maxPerStageDescriptorStorageImages);
        AddUintFlag(rep.limits, "maxPerStageResources", lim.maxPerStageResources);
        AddUintFlag(rep.limits, "maxDescriptorSetSamplers", lim.maxDescriptorSetSamplers);
        AddUintFlag(rep.limits, "maxDescriptorSetUniformBuffers", lim.maxDescriptorSetUniformBuffers);
        AddUintFlag(rep.limits, "maxDescriptorSetStorageBuffers", lim.maxDescriptorSetStorageBuffers);
        AddUintFlag(rep.limits, "maxDescriptorSetSampledImages", lim.maxDescriptorSetSampledImages);
        AddUintFlag(rep.limits, "maxDescriptorSetStorageImages", lim.maxDescriptorSetStorageImages);
        AddUintFlag(rep.limits, "minMemoryMapAlignment", lim.minMemoryMapAlignment);
        AddUintFlag(rep.limits, "minStorageBufferOffsetAlignment", lim.minStorageBufferOffsetAlignment);
        AddUintFlag(rep.limits, "minUniformBufferOffsetAlignment", lim.minUniformBufferOffsetAlignment);

        // Formats check
        struct FormatCheck {
            const char* name;
            VkFormat format;
        };
        const FormatCheck kChecks[] = {
            {"VK_FORMAT_R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
            {"VK_FORMAT_R8G8B8A8_SRGB", VK_FORMAT_R8G8B8A8_SRGB},
            {"VK_FORMAT_B8G8R8A8_UNORM", VK_FORMAT_B8G8R8A8_UNORM},
            {"VK_FORMAT_R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
            {"VK_FORMAT_R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
            {"VK_FORMAT_BC1_RGBA_UNORM_BLOCK", VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
            {"VK_FORMAT_BC3_UNORM_BLOCK", VK_FORMAT_BC3_UNORM_BLOCK},
            {"VK_FORMAT_BC7_UNORM_BLOCK", VK_FORMAT_BC7_UNORM_BLOCK},
            {"VK_FORMAT_ASTC_4x4_UNORM_BLOCK", VK_FORMAT_ASTC_4x4_UNORM_BLOCK},
            {"VK_FORMAT_D32_SFLOAT", VK_FORMAT_D32_SFLOAT},
            {"VK_FORMAT_D24_UNORM_S8_UINT", VK_FORMAT_D24_UNORM_S8_UINT},
            {"VK_FORMAT_D16_UNORM", VK_FORMAT_D16_UNORM},
        };

        for (const auto& fc : kChecks) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(pdev, fc.format, &fp);
            VulkanFormatReport fr;
            fr.name = fc.name;
            fr.value = static_cast<uint32_t>(fc.format);
            fr.linear_tiling_features = fp.linearTilingFeatures;
            fr.optimal_tiling_features = fp.optimalTilingFeatures;
            fr.buffer_features = fp.bufferFeatures;
            fr.optimal_capabilities = DecodeOptimalTilingFeatures(fp.optimalTilingFeatures);
            rep.formats.push_back(fr);
        }

        out.devices.push_back(rep);
    }

    vkDestroyInstance(instance, nullptr);
    out.status = "ok";
}

#else

void CollectVulkan(const ProbeContext& /*ctx*/, VulkanReport& out) {
    out.status = kUnavailableOnHost;
    out.error = "Vulkan probe skipped on macOS host; executed on Android target device.";
}

#endif

} // namespace ffrs
