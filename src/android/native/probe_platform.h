// Platform collector interface for the FFRS device capability probe.
//
// T03A (ANDROID). The dispatcher in probe_platform.cpp routes to the Android
// implementation (probe_android.cpp) or the host implementation
// (probe_host.cpp) depending on __ANDROID__.

#ifndef FFRS_PROBE_PLATFORM_H
#define FFRS_PROBE_PLATFORM_H

#include "probe_types.h"

namespace ffrs {

void CollectDevice(const ProbeContext& ctx, DeviceInfo& out);
void CollectMemory(const ProbeContext& ctx, MemoryInfo& out);
void CollectStorage(const ProbeContext& ctx, StorageInfo& out);
void CollectCpu(const ProbeContext& ctx, CpuInfo& out);
void CollectDisplay(const ProbeContext& ctx, DisplayInfo& out);
void CollectInput(const ProbeContext& ctx, InputInfo& out);
void CollectAudio(const ProbeContext& ctx, AudioInfo& out);

} // namespace ffrs

#endif // FFRS_PROBE_PLATFORM_H
