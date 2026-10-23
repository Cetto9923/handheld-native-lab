// Shared data structures for the FFRS read-only device capability probe.
//
// T03A (ANDROID). These structures are filled by the platform-specific
// collectors (Android / host) and rendered to JSON by probe_core.cpp. Keeping
// the data model separate from the platform code is what allows the exact
// same probe source to build for Android arm64-v8a and for the macOS host.

#ifndef FFRS_PROBE_TYPES_H
#define FFRS_PROBE_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace ffrs {

// Markers written into JSON fields that cannot be determined on the current
// build platform. They are plain strings so the document stays schema-valid
// and a consumer can detect "not measured here" without type guessing.
extern const char* const kUnavailableOnHost; // host build, needs an Android device
extern const char* const kUnavailable;       // Android build, reason unknown/unavailable

// Optional Java context. When activity_ref is null the probe runs "headless":
// it can still report Build/storage/CPU/Vulkan facts that do not need a
// Context, while Context-bound sections report unavailable_no_context.
struct ProbeContext {
    void* java_vm = nullptr;      // JavaVM*
    void* activity_ref = nullptr; // jobject (global ref) to the Activity
    const char* execution_mode = "host";

    bool has_java() const { return java_vm != nullptr && activity_ref != nullptr; }
};

struct DeviceInfo {
    std::string status = "unavailable";
    std::string android_release;
    std::string build_id;
    std::string fingerprint;
    std::string manufacturer;
    std::string brand;
    std::string model;
    std::string product;
    std::string board;
    std::string hardware;
    std::vector<std::string> abis;
    std::vector<std::string> abis64;
    bool is_64bit = false;
    int cpu_cores = 0;
};

struct MemoryInfo {
    std::string status = "unavailable";
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
    uint64_t threshold_bytes = 0;
    bool low_memory = false;
    uint64_t page_size_bytes = 0;
};

struct StorageInfo {
    std::string status = "unavailable";
    std::string path;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
};

struct CpuInfo {
    std::string status = "unavailable";
    std::string arch;
    std::string uname_machine;
    std::string cpu_family;
    int cores_online = 0;
    int cores_configured = 0;
    uint64_t page_size_bytes = 0;
    bool hwcap_available = false;
    uint64_t hwcap = 0;
    uint64_t hwcap2 = 0;
    bool android_cpu_features_available = false;
    uint64_t android_cpu_features = 0;
};

struct AxisInfo {
    int type = -1;
    std::string name;
    double min = 0.0;
    double max = 0.0;
    double flat = 0.0;
    double fuzz = 0.0;
};

struct InputDeviceInfo {
    int id = 0;
    std::string name;
    int vendor_id = 0;
    int product_id = 0;
    int controller_number = -1;
    std::vector<std::string> sources;
    bool is_gamepad = false;
    std::vector<AxisInfo> axes;
    std::vector<int> key_codes;
};

struct InputInfo {
    std::string status = "unavailable";
    std::vector<InputDeviceInfo> devices;
    int gamepad_count = 0;
};

struct DisplayModeInfo {
    int width_px = 0;
    int height_px = 0;
    double refresh_rate_hz = 0.0;
};

struct DisplayInfo {
    std::string status = "unavailable";
    int width_px = 0;
    int height_px = 0;
    double refresh_rate_hz = 0.0;
    int density_dpi = 0;
    double density = 0.0;
    std::vector<DisplayModeInfo> modes;
};

struct AudioInfo {
    std::string status = "unavailable";
    int output_device_count = 0;
    int sample_rate_hz = 0;
    int output_frames_per_buffer = 0;
    int native_output_sample_rate_hz = 0;
    int min_buffer_size_bytes = 0;
    bool audiotrack_create_ok = false;
    std::string audiotrack_error;
    std::string channel_config;
};

} // namespace ffrs

#endif // FFRS_PROBE_TYPES_H
