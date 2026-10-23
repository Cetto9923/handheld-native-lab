#if defined(__ANDROID__)

#include "probe_platform.h"
#include "probe_types.h"

#include <dirent.h>
#include <inttypes.h>
#include <fcntl.h>
#include <linux/input.h>
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

void AndroidCollectInput(const ProbeContext& /*ctx*/, InputInfo& out) {
    out.status = "ok";
    out.gamepad_count = 0;

    // First try /dev/input/event*
    for (int i = 0; i < 32; i++) {
        char dev_path[64];
        std::snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        InputDeviceInfo dev;
        dev.id = i;
        char name[128] = {0};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            dev.name = name;
        }

        struct input_id iid;
        if (ioctl(fd, EVIOCGID, &iid) >= 0) {
            dev.vendor_id = iid.vendor;
            dev.product_id = iid.product;
        }

        uint8_t ev_bits[(EV_MAX + 7) / 8] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0) {
            bool has_keys = (ev_bits[EV_KEY / 8] & (1 << (EV_KEY % 8))) != 0;
            bool has_abs = (ev_bits[EV_ABS / 8] & (1 << (EV_ABS % 8))) != 0;

            if (has_keys) {
                uint8_t key_bits[(KEY_MAX + 7) / 8] = {0};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
                    for (int k = 0; k < KEY_MAX; k++) {
                        if (key_bits[k / 8] & (1 << (k % 8))) {
                            dev.key_codes.push_back(k);
                            if (k >= BTN_MISC && k <= BTN_GEAR_UP) {
                                dev.is_gamepad = true;
                            }
                        }
                    }
                }
            }

            if (has_abs) {
                uint8_t abs_bits[(ABS_MAX + 7) / 8] = {0};
                if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
                    for (int a = 0; a < ABS_MAX; a++) {
                        if (abs_bits[a / 8] & (1 << (a % 8))) {
                            struct input_absinfo absinfo;
                            if (ioctl(fd, EVIOCGABS(a), &absinfo) >= 0) {
                                AxisInfo ax;
                                ax.type = a;
                                char ax_name[32];
                                std::snprintf(ax_name, sizeof(ax_name), "ABS_%d", a);
                                ax.name = ax_name;
                                ax.min = absinfo.minimum;
                                ax.max = absinfo.maximum;
                                ax.flat = absinfo.flat;
                                ax.fuzz = absinfo.fuzz;
                                dev.axes.push_back(ax);
                            }
                        }
                    }
                }
            }
        }

        if (dev.is_gamepad || !dev.axes.empty()) {
            dev.is_gamepad = true;
            out.gamepad_count++;
        }
        out.devices.push_back(dev);
        close(fd);
    }

    // Fallback: If /dev/input opened 0 devices due to permissions, query /sys/class/input/
    if (out.devices.empty()) {
        DIR* input_dir = opendir("/sys/class/input");
        if (input_dir) {
            struct dirent* entry;
            int idx = 0;
            while ((entry = readdir(input_dir)) != nullptr) {
                if (std::strncmp(entry->d_name, "event", 5) == 0) {
                    std::string base = std::string("/sys/class/input/") + entry->d_name + "/device/";
                    std::string name_path = base + "name";
                    std::ifstream nf(name_path);
                    if (nf.is_open()) {
                        InputDeviceInfo dev;
                        dev.id = idx++;
                        std::getline(nf, dev.name);
                        std::ifstream vid_file(base + "id/vendor");
                        if (vid_file.is_open()) {
                            std::string v; vid_file >> v;
                            dev.vendor_id = static_cast<int>(std::strtol(v.c_str(), nullptr, 16));
                        }
                        std::ifstream pid_file(base + "id/product");
                        if (pid_file.is_open()) {
                            std::string p; pid_file >> p;
                            dev.product_id = static_cast<int>(std::strtol(p.c_str(), nullptr, 16));
                        }
                        // Heuristic for RG477M / gamepads
                        std::string lower_name = dev.name;
                        for (auto& c : lower_name) c = std::tolower(c);
                        if (lower_name.find("gamepad") != std::string::npos ||
                            lower_name.find("controller") != std::string::npos ||
                            lower_name.find("joystick") != std::string::npos ||
                            lower_name.find("anbernic") != std::string::npos) {
                            dev.is_gamepad = true;
                            out.gamepad_count++;
                        }
                        out.devices.push_back(dev);
                    }
                }
            }
            closedir(input_dir);
        }
    }
}

void AndroidCollectAudio(const ProbeContext& /*ctx*/, AudioInfo& out) {
    out = AudioInfo{};
    out.status = "not_run_no_audiotrack_probe";
    out.audiotrack_error = "AudioTrack output and playback have not been measured; NOT_RUN.";
}

} // namespace ffrs

#endif // __ANDROID__
