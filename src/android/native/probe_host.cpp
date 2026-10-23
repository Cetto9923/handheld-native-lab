#include "probe_platform.h"
#include "probe_types.h"

#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace ffrs {

void HostCollectDevice(const ProbeContext& /*ctx*/, DeviceInfo& out) {
    out.status = "host_macos";
    struct utsname uts;
    if (uname(&uts) == 0) {
        out.hardware = uts.machine;
        out.model = uts.sysname;
        out.build_id = uts.release;
    }
    out.manufacturer = "Apple";
    out.brand = "Host";
    out.is_64bit = (sizeof(void*) == 8);
    out.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    out.abis.push_back("host-native");
}

void HostCollectMemory(const ProbeContext& /*ctx*/, MemoryInfo& out) {
    out.status = "ok";
    out.page_size_bytes = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
#if defined(HW_MEMSIZE)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctl(mib, 2, &mem, &len, nullptr, 0) == 0) {
        out.total_bytes = mem;
        out.available_bytes = mem / 2; // rough estimation for host
    }
#endif
}

void HostCollectStorage(const ProbeContext& /*ctx*/, StorageInfo& out) {
    out.status = "ok";
    out.path = ".";
    struct statvfs buf;
    if (statvfs(".", &buf) == 0) {
        out.total_bytes = static_cast<uint64_t>(buf.f_blocks) * buf.f_frsize;
        out.free_bytes = static_cast<uint64_t>(buf.f_bavail) * buf.f_frsize;
    }
}

void HostCollectCpu(const ProbeContext& /*ctx*/, CpuInfo& out) {
    out.status = "ok";
    struct utsname uts;
    if (uname(&uts) == 0) {
        out.uname_machine = uts.machine;
        out.arch = uts.machine;
    }
    out.cores_online = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    out.cores_configured = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
    out.page_size_bytes = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
}

void HostCollectInput(const ProbeContext& /*ctx*/, InputInfo& out) {
    out.status = kUnavailableOnHost;
}

void HostCollectAudio(const ProbeContext& /*ctx*/, AudioInfo& out) {
    out.status = kUnavailableOnHost;
}

} // namespace ffrs
