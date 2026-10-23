#if defined(__ANDROID__)

#include "probe_platform.h"
#include "probe_types.h"

#include <inttypes.h>
#include <sys/auxv.h>
#include <sys/statvfs.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ffrs {

static std::string GetSystemProperty(const char* name, const std::string& fallback = "") {
    char buf[PROP_VALUE_MAX] = {0};
    int len = __system_property_get(name, buf);
    if (len > 0) {
        return std::string(buf, len);
    }
    return fallback;
}

static std::vector<std::string> SplitCommaList(const std::string& str) {
    std::vector<std::string> res;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) res.push_back(item);
    }
    return res;
}

void AndroidCollectDevice(const ProbeContext& /*ctx*/, DeviceInfo& out) {
    out.status = "ok";
    out.android_release = GetSystemProperty("ro.build.version.release");
    out.build_id = GetSystemProperty("ro.build.id");
    out.fingerprint = GetSystemProperty("ro.build.fingerprint");
    out.manufacturer = GetSystemProperty("ro.product.manufacturer");
    out.brand = GetSystemProperty("ro.product.brand");
    out.model = GetSystemProperty("ro.product.model");
    out.product = GetSystemProperty("ro.board.platform", GetSystemProperty("ro.product.name"));
    out.board = GetSystemProperty("ro.product.board");
    out.hardware = GetSystemProperty("ro.hardware");

    std::string abilist = GetSystemProperty("ro.product.cpu.abilist");
    if (abilist.empty()) {
        abilist = GetSystemProperty("ro.product.cpu.abi");
    }
    out.abis = SplitCommaList(abilist);

    std::string abilist64 = GetSystemProperty("ro.product.cpu.abilist64");
    if (!abilist64.empty()) {
        out.abis64 = SplitCommaList(abilist64);
    }

    out.is_64bit = (sizeof(void*) == 8);
    out.cpu_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

void AndroidCollectMemory(const ProbeContext& /*ctx*/, MemoryInfo& out) {
    out.status = "ok";
    out.page_size_bytes = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    long phys_pages = sysconf(_SC_PHYS_PAGES);
    if (phys_pages > 0) {
        out.total_bytes = static_cast<uint64_t>(phys_pages) * out.page_size_bytes;
    }

    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        uint64_t mem_free_kb = 0;
        uint64_t buffers_kb = 0;
        uint64_t cached_kb = 0;
        uint64_t mem_avail_kb = 0;
        bool has_avail = false;

        while (std::getline(meminfo, line)) {
            char key[64];
            uint64_t val = 0;
            if (std::sscanf(line.c_str(), "%63s %" SCNu64, key, &val) >= 2) {
                if (std::strcmp(key, "MemAvailable:") == 0) {
                    mem_avail_kb = val;
                    has_avail = true;
                } else if (std::strcmp(key, "MemFree:") == 0) {
                    mem_free_kb = val;
                } else if (std::strcmp(key, "Buffers:") == 0) {
                    buffers_kb = val;
                } else if (std::strcmp(key, "Cached:") == 0) {
                    cached_kb = val;
                }
            }
        }
        if (has_avail) {
            out.available_bytes = mem_avail_kb * 1024;
        } else {
            out.available_bytes = (mem_free_kb + buffers_kb + cached_kb) * 1024;
        }
    }

    if (out.total_bytes > 0 && out.available_bytes > 0) {
        out.threshold_bytes = out.total_bytes / 10; // 10% threshold
        out.low_memory = (out.available_bytes < out.threshold_bytes);
    }
}

void AndroidCollectStorage(const ProbeContext& /*ctx*/, StorageInfo& out) {
    out.status = "ok";
    const char* paths[] = {"/data", "/sdcard", "/data/local/tmp", "."};
    for (const char* p : paths) {
        struct statvfs sv;
        if (statvfs(p, &sv) == 0 && sv.f_blocks > 0) {
            out.path = p;
            out.total_bytes = static_cast<uint64_t>(sv.f_blocks) * sv.f_frsize;
            out.free_bytes = static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize;
            return;
        }
    }
    out.status = "storage_query_failed";
}

void AndroidCollectCpu(const ProbeContext& /*ctx*/, CpuInfo& out) {
    out.status = "ok";
    out.arch = "arm64-v8a";
    struct utsname uts;
    if (uname(&uts) == 0) {
        out.uname_machine = uts.machine;
    }
    out.cores_online = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    out.cores_configured = static_cast<int>(sysconf(_SC_NPROCESSORS_CONF));
    out.page_size_bytes = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));

#if defined(AT_HWCAP)
    out.hwcap = getauxval(AT_HWCAP);
#endif
#if defined(AT_HWCAP2)
    out.hwcap2 = getauxval(AT_HWCAP2);
#endif
    out.hwcap_available = (out.hwcap != 0 || out.hwcap2 != 0);

    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.find("Hardware") != std::string::npos ||
                line.find("model name") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos && colon + 2 < line.size()) {
                    out.cpu_family = line.substr(colon + 2);
                    break;
                }
            }
        }
    }
    if (out.cpu_family.empty()) {
        out.cpu_family = GetSystemProperty("ro.board.platform", "ARM64");
    }
}

void AndroidCollectAudio(const ProbeContext& /*ctx*/, AudioInfo& out) {
    out = AudioInfo{};
    out.status = "not_run_no_audiotrack_probe";
    out.audiotrack_error = "AudioTrack output and playback have not been measured; NOT_RUN.";
}

} // namespace ffrs

#endif // __ANDROID__
