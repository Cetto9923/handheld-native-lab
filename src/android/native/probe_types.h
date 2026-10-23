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

struct DisplayInfo;
// UI-thread Android API snapshot, owned by the synchronous probe caller.
// A CLI has no Activity/window measurement and must not substitute a model.
struct ProbeContext {
    const DisplayInfo* display_snapshot = nullptr;
    const char* execution_mode = "host";
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
    int mode_id = 0;
    int width_px = 0;
    int height_px = 0;
    double refresh_rate_hz = 0.0;
};

struct DisplayInfo {
    std::string status = "unavailable";
    std::string size_source;
    std::string error;
    int width_px = 0;
    int height_px = 0;
    int content_width_px = 0;
    int content_height_px = 0;
    int inset_left_px = 0;
    int inset_top_px = 0;
    int inset_right_px = 0;
    int inset_bottom_px = 0;
    bool insets_measured = false;
    int display_id = -1;
    int rotation = -1;
    int current_mode_id = 0;
    double refresh_rate_hz = 0.0;
    std::string refresh_rate_status = "unavailable";
    int density_dpi = 0;
    double density = 0.0;
    uint64_t captured_elapsed_realtime_ns = 0;
    std::string modes_status = "unavailable";
    std::vector<DisplayModeInfo> modes;
};

// Validate a measured Activity snapshot; never fill unknown values by guess.
DisplayInfo NormalizeDisplaySnapshot(const DisplayInfo& measured);

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
