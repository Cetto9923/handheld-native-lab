// Vulkan capability report structures for the FFRS device capability probe.
//
// T03A (ANDROID). The Vulkan collector is the highest-value section for the
// project: the game is known to depend heavily on bindless / textureBuffer
// descriptor indexing, so descriptor-indexing features, the descriptor limits
// and the per-format properties are captured in full.
//
// The host (macOS) build has no Vulkan loader guarantee, so the collector
// reports "unavailable_on_host" there rather than fabricating values.

#ifndef FFRS_PROBE_VULKAN_H
#define FFRS_PROBE_VULKAN_H

#include "probe_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ffrs {

struct BoolFlags {
    std::vector<std::string> names;
    std::vector<bool> values;
};

struct UintFlags {
    std::vector<std::string> names;
    std::vector<uint64_t> values;
};

struct VulkanFormatReport {
    std::string name;
    uint32_t value = 0;
    uint32_t linear_tiling_features = 0;
    uint32_t optimal_tiling_features = 0;
    uint32_t buffer_features = 0;
    std::vector<std::string> optimal_capabilities;
};

struct VulkanPhysicalDeviceReport {
    int index = 0;
    std::string name;
    std::string type;
    uint32_t type_raw = 0;
    uint32_t vendor_id = 0;
    std::string vendor_name;
    uint32_t device_id = 0;

    uint32_t api_version_raw = 0;
    std::string api_version;
    uint32_t driver_version_raw = 0;
    std::string driver_version;
    std::string driver_name;
    std::string driver_info;
    uint32_t conformance_version_raw = 0;
    std::string conformance_version;

    std::vector<std::string> extensions;

    bool supports_portability_subset = false;
    BoolFlags portability_subset;

    BoolFlags features_core;
    BoolFlags features_vulkan11;
    BoolFlags features_vulkan12;
    BoolFlags features_vulkan13;
    BoolFlags descriptor_indexing;
    BoolFlags maintenance3;

    UintFlags limits;

    std::vector<VulkanFormatReport> formats;
};

struct VulkanReport {
    std::string status = "unavailable_on_host";
    std::string error;
    bool instance_version_query_supported = false;
    uint32_t instance_api_version_raw = 0;
    std::string instance_api_version;
    int physical_device_count = 0;
    std::vector<VulkanPhysicalDeviceReport> devices;
};

void CollectVulkan(const ProbeContext& ctx, VulkanReport& out);

} // namespace ffrs

#endif // FFRS_PROBE_VULKAN_H
