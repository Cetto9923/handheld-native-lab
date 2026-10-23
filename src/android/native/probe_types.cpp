#include "probe_types.h"
#include <cmath>
#include <set>

namespace ffrs {

const char* const kUnavailableOnHost = "unavailable_on_host";
const char* const kUnavailable = "unavailable";

InputInfo NormalizeInputSnapshot(const InputInfo& measured) {
    InputInfo out = measured;
    auto invalid = [&](const char* reason) {
        InputInfo unavailable;
        unavailable.status = "unavailable_invalid_input_snapshot";
        unavailable.error = reason;
        return unavailable;
    };
    if (!out.measured || out.source.empty()) {
        out = InputInfo{};
        out.status = "unavailable_input_not_measured";
        out.error = measured.error;
        return out;
    }
    std::set<int> ids;
    out.gamepad_count = 0;
    for (auto& device : out.devices) {
        if (!ids.insert(device.id).second || device.vendor_id < 0 || device.product_id < 0)
            return invalid("Duplicate input id or invalid vendor/product id.");
        // Android source constants include class bits; test the complete source mask.
        constexpr uint32_t gamepad = 0x00000401, joystick = 0x01000010;
        device.is_gamepad = (device.source_mask & gamepad) == gamepad
            || (device.source_mask & joystick) == joystick;
        if (device.is_gamepad) out.gamepad_count++;
        device.sources.clear();
        const struct { uint32_t mask; const char* name; } sources[] = {
            {0x00000101, "SOURCE_KEYBOARD"}, {0x00000201, "SOURCE_DPAD"},
            {gamepad, "SOURCE_GAMEPAD"}, {joystick, "SOURCE_JOYSTICK"},
            {0x00001002, "SOURCE_TOUCHSCREEN"}, {0x00002002, "SOURCE_MOUSE"},
            {0x00004002, "SOURCE_STYLUS"}, {0x00100008, "SOURCE_TOUCHPAD"}};
        for (const auto& source : sources)
            if ((device.source_mask & source.mask) == source.mask) device.sources.push_back(source.name);
        for (int key : device.key_codes) if (key < 0) return invalid("Negative Android keycode.");
        for (const auto& axis : device.axes) {
            if (axis.type < 0 || !std::isfinite(axis.min) || !std::isfinite(axis.max)
                    || !std::isfinite(axis.flat) || !std::isfinite(axis.fuzz) || !std::isfinite(axis.resolution)
                    || axis.max < axis.min || axis.flat < 0 || axis.fuzz < 0 || axis.resolution < 0)
                return invalid("Invalid Android MotionRange.");
        }
    }
    out.status = out.enumeration_complete && out.error.empty() ? "ok" : "partial_android_api_measurement";
    return out;
}

DisplayInfo NormalizeDisplaySnapshot(const DisplayInfo& measured) {
    DisplayInfo out = measured;
    if (out.width_px <= 0 || out.height_px <= 0 || out.size_source.empty()) {
        out = DisplayInfo{};
        out.status = "unavailable_invalid_activity_metrics";
        out.error = measured.error;
        return out;
    }
    if (!std::isfinite(out.refresh_rate_hz) || out.refresh_rate_hz <= 0) out.refresh_rate_hz = 0;
    out.refresh_rate_status = out.refresh_rate_hz > 0 ? "measured_android_api" : "unavailable";
    if (out.density_dpi <= 0 || !std::isfinite(out.density) || out.density <= 0) {
        out.density_dpi = 0;
        out.density = 0;
    }
    if (out.content_width_px <= 0 || out.content_height_px <= 0) {
        out.content_width_px = out.content_height_px = 0;
    }
    if (!out.insets_measured || out.insets_source.empty() || out.inset_left_px < 0
            || out.inset_top_px < 0 || out.inset_right_px < 0 || out.inset_bottom_px < 0) {
        out.insets_measured = false;
        out.insets_source.clear();
        out.inset_left_px = out.inset_top_px = out.inset_right_px = out.inset_bottom_px = 0;
    }
    out.modes.clear();
    for (const auto& mode : measured.modes) {
        if (mode.mode_id > 0 && mode.width_px > 0 && mode.height_px > 0
                && std::isfinite(mode.refresh_rate_hz) && mode.refresh_rate_hz > 0)
            out.modes.push_back(mode);
    }
    out.modes_status = out.modes.empty() ? "unavailable" : "measured_android_api";
    out.status = out.refresh_rate_hz > 0 && out.density_dpi > 0 && out.error.empty()
        ? "ok" : "partial_android_api_measurement";
    return out;
}

} // namespace ffrs
