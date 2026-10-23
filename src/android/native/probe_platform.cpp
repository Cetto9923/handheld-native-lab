#include "probe_platform.h"

namespace ffrs {

void CollectDisplay(const ProbeContext& ctx, DisplayInfo& out) {
    if (ctx.display_snapshot) {
        out = NormalizeDisplaySnapshot(*ctx.display_snapshot);
    } else {
        out = DisplayInfo{};
        out.status = "unavailable_no_activity_context";
    }
}

#if defined(__ANDROID__)

void AndroidCollectDevice(const ProbeContext& ctx, DeviceInfo& out);
void AndroidCollectMemory(const ProbeContext& ctx, MemoryInfo& out);
void AndroidCollectStorage(const ProbeContext& ctx, StorageInfo& out);
void AndroidCollectCpu(const ProbeContext& ctx, CpuInfo& out);
void AndroidCollectInput(const ProbeContext& ctx, InputInfo& out);
void AndroidCollectAudio(const ProbeContext& ctx, AudioInfo& out);

void CollectDevice(const ProbeContext& ctx, DeviceInfo& out) { AndroidCollectDevice(ctx, out); }
void CollectMemory(const ProbeContext& ctx, MemoryInfo& out) { AndroidCollectMemory(ctx, out); }
void CollectStorage(const ProbeContext& ctx, StorageInfo& out) { AndroidCollectStorage(ctx, out); }
void CollectCpu(const ProbeContext& ctx, CpuInfo& out) { AndroidCollectCpu(ctx, out); }
void CollectInput(const ProbeContext& ctx, InputInfo& out) { AndroidCollectInput(ctx, out); }
void CollectAudio(const ProbeContext& ctx, AudioInfo& out) { AndroidCollectAudio(ctx, out); }

#else

void HostCollectDevice(const ProbeContext& ctx, DeviceInfo& out);
void HostCollectMemory(const ProbeContext& ctx, MemoryInfo& out);
void HostCollectStorage(const ProbeContext& ctx, StorageInfo& out);
void HostCollectCpu(const ProbeContext& ctx, CpuInfo& out);
void HostCollectInput(const ProbeContext& ctx, InputInfo& out);
void HostCollectAudio(const ProbeContext& ctx, AudioInfo& out);

void CollectDevice(const ProbeContext& ctx, DeviceInfo& out) { HostCollectDevice(ctx, out); }
void CollectMemory(const ProbeContext& ctx, MemoryInfo& out) { HostCollectMemory(ctx, out); }
void CollectStorage(const ProbeContext& ctx, StorageInfo& out) { HostCollectStorage(ctx, out); }
void CollectCpu(const ProbeContext& ctx, CpuInfo& out) { HostCollectCpu(ctx, out); }
void CollectInput(const ProbeContext& ctx, InputInfo& out) { HostCollectInput(ctx, out); }
void CollectAudio(const ProbeContext& ctx, AudioInfo& out) { HostCollectAudio(ctx, out); }

#endif

} // namespace ffrs
