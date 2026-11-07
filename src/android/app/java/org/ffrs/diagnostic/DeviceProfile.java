package org.ffrs.diagnostic;

import java.util.Locale;

/** Conservative preset selection; actual rendering and present rates remain separately measured. */
final class DeviceProfile {
    final String id;
    final int targetFps;
    final int targetShortEdge;

    private DeviceProfile(String id, int targetShortEdge, int targetFps) {
        this.id = id;
        this.targetShortEdge = targetShortEdge;
        this.targetFps = targetFps;
    }

    boolean usesDockedRendering() {
        return id.equals("tablet_sd8s") || id.equals("phone_sd8g3");
    }

    static DeviceProfile select(String soc, int smallestWidthDp, String override) {
        String id = override == null ? "" : override.trim().toLowerCase(Locale.ROOT);
        if (id.equals("handheld_mid")) return new DeviceProfile(id, 720, 30);
        if (id.equals("tablet_sd8s")) return new DeviceProfile(id, 1080, 60);
        if (id.equals("phone_sd8g3")) return new DeviceProfile(id, 1080, 60);
        if (id.equals("odin_elite")) return new DeviceProfile(id, 1080, 30);
        String chip = soc == null ? "" : soc.trim().toLowerCase(Locale.ROOT);
        if (chip.equals("sm8635") && smallestWidthDp >= 600) return new DeviceProfile("tablet_sd8s", 1080, 60);
        // Honor 400 Pro. This selects a target; phone performance needs measurement.
        if (chip.equals("sm8650")) return new DeviceProfile("phone_sd8g3", 1080, 60);
        return new DeviceProfile("handheld_mid", 720, 30);
    }
}
