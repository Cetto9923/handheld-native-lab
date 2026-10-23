#include "probe_types.h"
#include <cmath>

namespace ffrs {

const char* const kUnavailableOnHost = "unavailable_on_host";
const char* const kUnavailable = "unavailable";

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
