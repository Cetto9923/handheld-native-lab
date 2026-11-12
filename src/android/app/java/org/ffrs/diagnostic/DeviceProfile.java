package org.ffrs.diagnostic;

import java.util.Locale;

/**
 * Conservative preset selection; actual rendering and present rates remain separately measured.
 *
 * The profile carries the resolution tier (the short edge of the internal
 * render target) and the target frame rate. The tier is not a label: it
 * decides the operation mode, and the guest's render target follows from
 * that. The frame rate is what the app declares to the platform.
 */
final class DeviceProfile {
    final String id;
    final int targetFps;
    final int targetShortEdge;

    private DeviceProfile(String id, int targetShortEdge, int targetFps) {
        this.id = id;
        this.targetShortEdge = targetShortEdge;
        this.targetFps = targetFps;
    }

    /**
     * The internal render target the tier stands for.
     *
     * The game is 16:9, so the short edge of the tier is the short edge of
     * the render target: 720 gives 1280x720 (the handheld tier) and 1080
     * gives 1920x1080 (the docked tier). The present path keeps this shape
     * when it puts the picture on a surface of another shape, so the tier is
     * a render size and not a label.
     */
    int renderTierWidth() { return targetShortEdge * 16 / 9; }

    int renderTierHeight() { return targetShortEdge; }

    /**
     * The operation mode the tier needs.
     *
     * The guest reads the operation mode and picks its own render target
     * from it: 1080 is the docked target, 720 the handheld one. This is
     * where the resolution tier is actually applied, so the tier and the
     * operation mode can no longer disagree.
     */
    boolean usesDockedRendering() {
        return targetShortEdge >= 1080;
    }

    /**
     * Whether this profile declares its frame rate to the platform.
     *
     * The handheld tier runs at 30 FPS on a panel that is usually faster
     * than that and says so, so the scheduler places the frames instead of
     * guessing. The tablet and phone tiers keep their own display mode
     * request, which already tells the platform what the window runs at,
     * and their accepted behaviour is left alone.
     */
    boolean declaresSurfaceFrameRate() {
        return targetShortEdge <= 720;
    }

    /**
     * The frame rate the app declares to the platform.
     *
     * A display mode request wins: it already tells the platform which
     * refresh rate the window runs at. When there is none, the game's own
     * target is declared, so a 30 FPS game on a 60 Hz panel says so instead
     * of letting the platform guess. Zero means: declare nothing.
     */
    int surfaceFrameRateRequest(int displayModeHz, int gameTargetFps) {
        if (displayModeHz > 0) return displayModeHz;
        return gameTargetFps > 0 ? gameTargetFps : 0;
    }

    // A freeform window can be narrower than 600dp on the Honor tablet. Its
    // size must not disable the device's required GPU compatibility path.
    static int formFactorWidthDp(int physicalWidth, int physicalHeight, int stableDensityDpi,
                                 int windowWidthDp) {
        if (physicalWidth <= 0 || physicalHeight <= 0 || stableDensityDpi <= 0)
            return windowWidthDp;
        return (int)Math.min(Integer.MAX_VALUE,
                (long)Math.min(physicalWidth, physicalHeight) * 160 / stableDensityDpi);
    }

    static DeviceProfile select(String soc, int smallestWidthDp, String override) {
        return select(soc, null, smallestWidthDp, override);
    }

    static DeviceProfile select(String soc, String model, int smallestWidthDp, String override) {
        String id = override == null ? "" : override.trim().toLowerCase(Locale.ROOT);
        if (id.equals("handheld_mid")) return new DeviceProfile(id, 720, 30);
        if (id.equals("tablet_sd8s")) return new DeviceProfile(id, 1080, 60);
        if (id.equals("phone_sd8g3")) return new DeviceProfile(id, 1080, 60);
        if (id.equals("odin_elite")) return new DeviceProfile(id, 1080, 30);

        String mod = model == null ? "" : model.trim().toLowerCase(Locale.ROOT);
        if (mod.contains("rg477m") || mod.contains("anbernic") || mod.contains("rg-477m")) {
            return new DeviceProfile("handheld_mid", 720, 30);
        }

        String chip = soc == null ? "" : soc.trim().toLowerCase(Locale.ROOT);
        if (chip.contains("6897") || chip.contains("dimensity 8300") || chip.contains("dimensity 8200")
                || chip.contains("rg477m") || chip.equals("handheld_mid")) {
            return new DeviceProfile("handheld_mid", 720, 30);
        }

        if (chip.equals("sm8635") && smallestWidthDp >= 600) return new DeviceProfile("tablet_sd8s", 1080, 60);
        // Honor 400 Pro, OnePlus Ace (SM8750 / Snapdragon 8 Elite), Snapdragon 8 series, and general phones.
        if (chip.equals("sm8650") || chip.equals("sm8750") || chip.equals("sm8550")
                || chip.equals("sm8475") || chip.equals("sm8450") || chip.equals("sm7675")
                || chip.equals("sm7475") || chip.startsWith("mt69") || chip.startsWith("kirin")
                || (!chip.isEmpty() && !chip.equals("mt6897") && !chip.equals("sm8635")
                    && !chip.equals("sm86350") && !chip.equals("sm86500") && smallestWidthDp > 0)) {
            return new DeviceProfile("phone_sd8g3", 1080, 60);
        }
        return new DeviceProfile("handheld_mid", 720, 30);
    }
}
