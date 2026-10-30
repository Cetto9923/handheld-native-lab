#include "probe_core.h"
#include "json_writer.h"

#include <cstdio>
#include <iostream>

namespace ffrs {

FullProbeReport RunProbe(const ProbeContext& ctx) {
    FullProbeReport rep;
    CollectDevice(ctx, rep.device);
    CollectMemory(ctx, rep.memory);
    CollectStorage(ctx, rep.storage);
    CollectCpu(ctx, rep.cpu);
    CollectDisplay(ctx, rep.display);
    CollectInput(ctx, rep.input);
    CollectAudio(ctx, rep.audio);
    CollectVulkan(ctx, rep.vulkan);
    return rep;
}

static void SerializeBoolFlags(JsonWriter& w, const char* key, const BoolFlags& flags) {
    w.Key(key);
    w.BeginObject();
    for (size_t i = 0; i < flags.names.size() && i < flags.values.size(); i++) {
        w.Key(flags.names[i].c_str());
        w.Bool(flags.values[i]);
    }
    w.EndObject();
}

static void SerializeUintFlags(JsonWriter& w, const char* key, const UintFlags& flags) {
    w.Key(key);
    w.BeginObject();
    for (size_t i = 0; i < flags.names.size() && i < flags.values.size(); i++) {
        w.Key(flags.names[i].c_str());
        w.UInt(flags.values[i]);
    }
    w.EndObject();
}

std::string SerializeProbeReportToJson(const FullProbeReport& rep) {
    JsonWriter w;
    w.BeginObject();

    // Section: target_metadata
    w.Key("target_metadata");
    w.BeginObject();
    w.Key("project");
    w.String("ff-resonance-rg477m-native");
    w.Key("target_device");
    w.String("Anbernic RG477M");
    w.Key("target_soc");
    w.String("MediaTek Dimensity 8300");
    w.Key("target_gpu");
    w.String("Mali-G615 MC6");
    w.Key("target_abi");
    w.String("arm64-v8a");
    w.EndObject();

    // Section: device
    w.Key("device");
    w.BeginObject();
    w.Key("status");
    w.String(rep.device.status);
    w.Key("android_release");
    w.String(rep.device.android_release);
    w.Key("build_id");
    w.String(rep.device.build_id);
    w.Key("fingerprint");
    w.String(rep.device.fingerprint);
    w.Key("manufacturer");
    w.String(rep.device.manufacturer);
    w.Key("brand");
    w.String(rep.device.brand);
    w.Key("model");
    w.String(rep.device.model);
    w.Key("product");
    w.String(rep.device.product);
    w.Key("board");
    w.String(rep.device.board);
    w.Key("hardware");
    w.String(rep.device.hardware);
    w.Key("is_64bit");
    w.Bool(rep.device.is_64bit);
    w.Key("cpu_cores");
    w.Int(rep.device.cpu_cores);
    w.Key("abis");
    w.BeginArray();
    for (const auto& abi : rep.device.abis) {
        w.String(abi);
    }
    w.EndArray();
    w.Key("abis64");
    w.BeginArray();
    for (const auto& abi : rep.device.abis64) {
        w.String(abi);
    }
    w.EndArray();
    w.EndObject();

    // Section: memory
    w.Key("memory");
    w.BeginObject();
    w.Key("status");
    w.String(rep.memory.status);
    w.Key("total_bytes");
    w.UInt(rep.memory.total_bytes);
    w.Key("available_bytes");
    w.UInt(rep.memory.available_bytes);
    w.Key("threshold_bytes");
    w.UInt(rep.memory.threshold_bytes);
    w.Key("low_memory");
    w.Bool(rep.memory.low_memory);
    w.Key("page_size_bytes");
    w.UInt(rep.memory.page_size_bytes);
    w.EndObject();

    // Section: storage
    w.Key("storage");
    w.BeginObject();
    w.Key("status");
    w.String(rep.storage.status);
    w.Key("path");
    w.String(rep.storage.path);
    w.Key("total_bytes");
    w.UInt(rep.storage.total_bytes);
    w.Key("free_bytes");
    w.UInt(rep.storage.free_bytes);
    w.EndObject();

    // Section: cpu
    w.Key("cpu");
    w.BeginObject();
    w.Key("status");
    w.String(rep.cpu.status);
    w.Key("arch");
    w.String(rep.cpu.arch);
    w.Key("uname_machine");
    w.String(rep.cpu.uname_machine);
    w.Key("cpu_family");
    w.String(rep.cpu.cpu_family);
    w.Key("cores_online");
    w.Int(rep.cpu.cores_online);
    w.Key("cores_configured");
    w.Int(rep.cpu.cores_configured);
    w.Key("page_size_bytes");
    w.UInt(rep.cpu.page_size_bytes);
    w.Key("hwcap_available");
    w.Bool(rep.cpu.hwcap_available);
    w.Key("hwcap");
    w.UInt(rep.cpu.hwcap);
    w.Key("hwcap2");
    w.UInt(rep.cpu.hwcap2);
    w.Key("android_cpu_features_available");
    w.Bool(rep.cpu.android_cpu_features_available);
    w.Key("android_cpu_features");
    w.UInt(rep.cpu.android_cpu_features);
    w.EndObject();

    // Section: display
    w.Key("display");
    w.BeginObject();
    w.Key("status");
    w.String(rep.display.status);
    w.Key("size_source"); w.String(rep.display.size_source);
    w.Key("size_scope"); w.String("activity_window_bounds");
    w.Key("error"); w.String(rep.display.error);
    w.Key("width_px");
    w.Int(rep.display.width_px);
    w.Key("height_px");
    w.Int(rep.display.height_px);
    w.Key("content_width_px"); w.Int(rep.display.content_width_px);
    w.Key("content_height_px"); w.Int(rep.display.content_height_px);
    w.Key("system_bar_cutout_insets_px"); w.BeginObject();
    w.Key("status"); w.String(rep.display.insets_measured ? "measured_android_api" : "unavailable");
    w.Key("source"); w.String(rep.display.insets_source);
    w.Key("scope"); w.String("system_bars_and_display_cutout_ignoring_visibility_excluding_ime");
    w.Key("left"); w.Int(rep.display.inset_left_px);
    w.Key("top"); w.Int(rep.display.inset_top_px);
    w.Key("right"); w.Int(rep.display.inset_right_px);
    w.Key("bottom"); w.Int(rep.display.inset_bottom_px);
    w.EndObject();
    w.Key("display_id"); w.Int(rep.display.display_id);
    w.Key("rotation"); w.Int(rep.display.rotation);
    w.Key("current_mode_id"); w.Int(rep.display.current_mode_id);
    w.Key("captured_elapsed_realtime_ns"); w.UInt(rep.display.captured_elapsed_realtime_ns);
    w.Key("refresh_rate_hz");
    w.Double(rep.display.refresh_rate_hz);
    w.Key("refresh_rate_status"); w.String(rep.display.refresh_rate_status);
    w.Key("refresh_rate_scope"); w.String("display_refresh_not_game_fps");
    w.Key("density_dpi");
    w.Int(rep.display.density_dpi);
    w.Key("density");
    w.Double(rep.display.density);
    w.Key("modes_status"); w.String(rep.display.modes_status);
    w.Key("mode_dimensions_scope"); w.String("physical_display_mode_not_activity_window");
    w.Key("modes");
    w.BeginArray();
    for (const auto& m : rep.display.modes) {
        w.BeginObject();
        w.Key("mode_id"); w.Int(m.mode_id);
        w.Key("width_px");
        w.Int(m.width_px);
        w.Key("height_px");
        w.Int(m.height_px);
        w.Key("refresh_rate_hz");
        w.Double(m.refresh_rate_hz);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    // Section: input
    w.Key("input");
    w.BeginObject();
    w.Key("status");
    w.String(rep.input.status);
    w.Key("source"); w.String(rep.input.source);
    w.Key("error"); w.String(rep.input.error);
    w.Key("enumeration_complete"); w.Bool(rep.input.enumeration_complete);
    w.Key("key_code_namespace"); w.String("android.view.KeyEvent.KEYCODE_*");
    w.Key("axis_code_namespace"); w.String("android.view.MotionEvent.AXIS_*");
    w.Key("scope"); w.String("reported_device_capabilities_not_event_or_game_mapping_validation");
    w.Key("captured_elapsed_realtime_ns"); w.UInt(rep.input.captured_elapsed_realtime_ns);
    w.Key("gamepad_count_status");
    w.String(!rep.input.measured ? "unavailable" : rep.input.enumeration_complete && rep.input.error.empty()
        ? "measured_android_api" : "partial_android_api_measurement");
    w.Key("gamepad_count");
    w.Int(rep.input.gamepad_count);
    w.Key("gamepad_count_unit"); w.String("android_input_devices_supporting_gamepad_or_joystick");
    w.Key("devices");
    w.BeginArray();
    for (const auto& dev : rep.input.devices) {
        w.BeginObject();
        w.Key("id");
        w.Int(dev.id);
        w.Key("name");
        w.String(dev.name);
        w.Key("vendor_id");
        w.Int(dev.vendor_id);
        w.Key("product_id");
        w.Int(dev.product_id);
        w.Key("controller_number");
        w.Int(dev.controller_number);
        w.Key("is_gamepad");
        w.Bool(dev.is_gamepad);
        w.Key("is_virtual"); w.Bool(dev.is_virtual);
        w.Key("source_mask"); w.UInt(dev.source_mask);
        w.Key("sources");
        w.BeginArray();
        for (const auto& src : dev.sources) {
            w.String(src);
        }
        w.EndArray();
        w.Key("axes");
        w.BeginArray();
        for (const auto& ax : dev.axes) {
            w.BeginObject();
            w.Key("type");
            w.Int(ax.type);
            w.Key("name");
            w.String(ax.name);
            w.Key("source_mask"); w.UInt(ax.source_mask);
            w.Key("resolution"); w.Double(ax.resolution);
            w.Key("min");
            w.Double(ax.min);
            w.Key("max");
            w.Double(ax.max);
            w.Key("flat");
            w.Double(ax.flat);
            w.Key("fuzz");
            w.Double(ax.fuzz);
            w.EndObject();
        }
        w.EndArray();
        w.Key("key_codes");
        w.BeginArray();
        for (int kc : dev.key_codes) {
            w.Int(kc);
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    // Section: audio
    w.Key("audio");
    w.BeginObject();
    w.Key("status");
    w.String(rep.audio.status);
    w.Key("output_device_count");
    w.Int(rep.audio.output_device_count);
    w.Key("sample_rate_hz");
    w.Int(rep.audio.sample_rate_hz);
    w.Key("output_frames_per_buffer");
    w.Int(rep.audio.output_frames_per_buffer);
    w.Key("native_output_sample_rate_hz");
    w.Int(rep.audio.native_output_sample_rate_hz);
    w.Key("min_buffer_size_bytes");
    w.Int(rep.audio.min_buffer_size_bytes);
    w.Key("audiotrack_create_ok");
    w.Bool(rep.audio.audiotrack_create_ok);
    w.Key("audiotrack_error");
    w.String(rep.audio.audiotrack_error);
    w.Key("channel_config");
    w.String(rep.audio.channel_config);
    w.EndObject();

    // Section: vulkan
    w.Key("vulkan");
    w.BeginObject();
    w.Key("status");
    w.String(rep.vulkan.status);
    w.Key("error");
    w.String(rep.vulkan.error);
    w.Key("instance_version_query_supported");
    w.Bool(rep.vulkan.instance_version_query_supported);
    w.Key("instance_api_version_raw");
    w.UInt(rep.vulkan.instance_api_version_raw);
    w.Key("instance_api_version");
    w.String(rep.vulkan.instance_api_version);
    w.Key("physical_device_count");
    w.Int(rep.vulkan.physical_device_count);
    w.Key("devices");
    w.BeginArray();
    for (const auto& dev : rep.vulkan.devices) {
        w.BeginObject();
        w.Key("index");
        w.Int(dev.index);
        w.Key("name");
        w.String(dev.name);
        w.Key("type");
        w.String(dev.type);
        w.Key("type_raw");
        w.UInt(dev.type_raw);
        w.Key("vendor_id");
        w.UInt(dev.vendor_id);
        w.Key("vendor_name");
        w.String(dev.vendor_name);
        w.Key("device_id");
        w.UInt(dev.device_id);
        w.Key("api_version_raw");
        w.UInt(dev.api_version_raw);
        w.Key("api_version");
        w.String(dev.api_version);
        w.Key("driver_version_raw");
        w.UInt(dev.driver_version_raw);
        w.Key("driver_version");
        w.String(dev.driver_version);
        w.Key("driver_name");
        w.String(dev.driver_name);
        w.Key("driver_info");
        w.String(dev.driver_info);
        w.Key("conformance_version_raw");
        w.UInt(dev.conformance_version_raw);
        w.Key("conformance_version");
        w.String(dev.conformance_version);
        w.Key("supports_portability_subset");
        w.Bool(dev.supports_portability_subset);

        w.Key("extensions");
        w.BeginArray();
        for (const auto& ext : dev.extensions) {
            w.String(ext);
        }
        w.EndArray();

        SerializeBoolFlags(w, "features_core", dev.features_core);
        SerializeBoolFlags(w, "features_vulkan11", dev.features_vulkan11);
        SerializeBoolFlags(w, "features_vulkan12", dev.features_vulkan12);
        SerializeBoolFlags(w, "features_vulkan13", dev.features_vulkan13);
        SerializeBoolFlags(w, "descriptor_indexing", dev.descriptor_indexing);
        SerializeBoolFlags(w, "maintenance3", dev.maintenance3);
        SerializeBoolFlags(w, "portability_subset", dev.portability_subset);

        SerializeUintFlags(w, "limits", dev.limits);

        w.Key("formats");
        w.BeginArray();
        for (const auto& fmt : dev.formats) {
            w.BeginObject();
            w.Key("name");
            w.String(fmt.name);
            w.Key("value");
            w.UInt(fmt.value);
            w.Key("linear_tiling_features");
            w.UInt(fmt.linear_tiling_features);
            w.Key("optimal_tiling_features");
            w.UInt(fmt.optimal_tiling_features);
            w.Key("buffer_features");
            w.UInt(fmt.buffer_features);
            w.Key("optimal_capabilities");
            w.BeginArray();
            for (const auto& cap : fmt.optimal_capabilities) {
                w.String(cap);
            }
            w.EndArray();
            w.EndObject();
        }
        w.EndArray();

        w.EndObject();
    }
    w.EndArray();
    w.EndObject();

    w.EndObject();
    return w.str();
}

void PrintReportSummary(const FullProbeReport& rep) {
    std::cout << "====================================================\n";
    std::cout << " RG477M Native Hardware & Capabilities Probe\n";
    std::cout << "====================================================\n";
    std::cout << "Device:   " << (rep.device.manufacturer.empty() ? "(unknown)" : rep.device.manufacturer)
              << " " << rep.device.model
              << " (Android " << rep.device.android_release << ", Build " << rep.device.build_id << ")\n";
    std::cout << "Platform: Hardware=" << rep.device.hardware << ", Board=" << rep.device.board << "\n";
    std::cout << "CPU:      " << rep.cpu.arch << " (" << rep.cpu.cores_online << " online / "
              << rep.cpu.cores_configured << " total cores), page size: " << rep.cpu.page_size_bytes << " B\n";
    std::cout << "Memory:   Total=" << (rep.memory.total_bytes / (1024 * 1024)) << " MB, Available="
              << (rep.memory.available_bytes / (1024 * 1024)) << " MB\n";
    std::cout << "Display:  " << rep.display.width_px << "x" << rep.display.height_px
              << " @" << rep.display.refresh_rate_hz << "Hz, density=" << rep.display.density_dpi << " dpi\n";
    std::cout << "Input:    Status=" << rep.input.status;
    if (rep.input.measured) std::cout << ", reported gamepad/joystick InputDevices=" << rep.input.gamepad_count;
    else std::cout << ", gamepad count unavailable";
    std::cout << '\n';
    std::cout << "Vulkan:   Status=" << rep.vulkan.status << ", API Version=" << rep.vulkan.instance_api_version << "\n";
    if (rep.vulkan.physical_device_count > 0) {
        for (const auto& dev : rep.vulkan.devices) {
            std::cout << "  - GPU: " << dev.name << " (" << dev.vendor_name << ", driver " << dev.driver_version << ")\n";
            std::cout << "    Driver: " << dev.driver_name << " " << dev.driver_info << "\n";
        }
    }
    std::cout << "====================================================\n";
}

} // namespace ffrs
