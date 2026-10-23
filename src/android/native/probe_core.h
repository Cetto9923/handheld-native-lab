#ifndef FFRS_PROBE_CORE_H
#define FFRS_PROBE_CORE_H

#include "probe_platform.h"
#include "probe_types.h"
#include "probe_vulkan.h"

#include <string>

namespace ffrs {

struct FullProbeReport {
    DeviceInfo device;
    MemoryInfo memory;
    StorageInfo storage;
    CpuInfo cpu;
    DisplayInfo display;
    InputInfo input;
    AudioInfo audio;
    VulkanReport vulkan;
};

// Runs all probe collectors under the given context.
FullProbeReport RunProbe(const ProbeContext& ctx);

// Serializes the full probe report into a deterministic JSON string.
std::string SerializeProbeReportToJson(const FullProbeReport& report);

// Emits a human-readable high-level summary to stdout.
void PrintReportSummary(const FullProbeReport& report);

} // namespace ffrs

#endif // FFRS_PROBE_CORE_H
