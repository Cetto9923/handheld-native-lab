package org.ffrs.diagnostic;
public class DeviceProfileContract {
    private static void check(String soc, int widthDp, String override, String id, int edge, int fps) {
        DeviceProfile p = DeviceProfile.select(soc, widthDp, override);
        if (!p.id.equals(id) || p.targetShortEdge != edge || p.targetFps != fps)
            throw new AssertionError(soc + ": " + p.id);
    }
    public static void main(String[] args) {
        int tabletWidth = DeviceProfile.formFactorWidthDp(3000, 1920, 400, 580);
        if (tabletWidth != 768) throw new AssertionError("Honor physical width");
        check("SM8635", tabletWidth, "", "tablet_sd8s", 1080, 60);
        check("SM8635", DeviceProfile.formFactorWidthDp(1920, 3000, 400, 420), "", "tablet_sd8s", 1080, 60);
        check("SM8635", DeviceProfile.formFactorWidthDp(1080, 2400, 420, 580), "", "handheld_mid", 720, 30);
        check("SM8635", tabletWidth, "handheld_mid", "handheld_mid", 720, 30);
        if (DeviceProfile.formFactorWidthDp(0, 0, 320, 580) != 580
                || DeviceProfile.formFactorWidthDp(3000, 1920, 0, 580) != 580
                || DeviceProfile.formFactorWidthDp(1200, 2000, 320, 0) != 600)
            throw new AssertionError("physical form factor fallback/boundary");
        check("SM8635", 768, "", "tablet_sd8s", 1080, 60);
        check("MT6897", 420, "", "handheld_mid", 720, 30);
        check("dimensity 8300", 420, "", "handheld_mid", 720, 30);
        DeviceProfile rg = DeviceProfile.select("unknown", "RG477M", 420, "");
        if (!rg.id.equals("handheld_mid") || rg.targetShortEdge != 720 || rg.targetFps != 30)
            throw new AssertionError("RG477M model selection");
        check(null, 0, null, "handheld_mid", 720, 30);
        check("SM8635", 768, "handheld_mid", "handheld_mid", 720, 30);
        check("MT6897", 420, "odin_elite", "odin_elite", 1080, 30);
        check("SM8635", 768, "invalid", "tablet_sd8s", 1080, 60);
        check("SM8635", 420, "", "handheld_mid", 720, 30);
        check("SM86350", 768, "", "handheld_mid", 720, 30);
        check("SM8635", 0, "", "handheld_mid", 720, 30);
        check("SM8635", 420, "tablet_sd8s", "tablet_sd8s", 1080, 60);
        check("SM8650", 420, "", "phone_sd8g3", 1080, 60);
        check("SM8750", 420, "", "phone_sd8g3", 1080, 60);
        check("SM86500", 420, "", "handheld_mid", 720, 30);
        check("SM8650", 420, "handheld_mid", "handheld_mid", 720, 30);
        if (!DeviceProfile.select("SM8650", 420, "").usesDockedRendering()
                || DeviceProfile.select(null, 0, null).usesDockedRendering())
            throw new AssertionError("docked rendering selection");
        /* The resolution tier is the render target the profile stands for,
         * and it decides the operation mode the guest renders in. */
        DeviceProfile handheld = DeviceProfile.select("MT6897", 420, "");
        if (handheld.renderTierWidth() != 1280 || handheld.renderTierHeight() != 720)
            throw new AssertionError("handheld render tier");
        DeviceProfile tablet = DeviceProfile.select("SM8635", 768, "");
        if (tablet.renderTierWidth() != 1920 || tablet.renderTierHeight() != 1080)
            throw new AssertionError("tablet render tier");
        if (handheld.usesDockedRendering() || !tablet.usesDockedRendering())
            throw new AssertionError("the tier decides the operation mode");
        /* A display mode request wins; without one the game target is
         * declared; without a target nothing is declared. */
        if (handheld.surfaceFrameRateRequest(0, 30) != 30)
            throw new AssertionError("no display mode request declares the game target");
        if (handheld.surfaceFrameRateRequest(60, 30) != 60)
            throw new AssertionError("a display mode request wins");
        if (handheld.surfaceFrameRateRequest(0, 0) != 0)
            throw new AssertionError("no target declares nothing");
        /* Only the handheld tier declares; the tablet and phone tiers keep
         * their own display mode request and their accepted behaviour. */
        if (!handheld.declaresSurfaceFrameRate() || tablet.declaresSurfaceFrameRate())
            throw new AssertionError("the frame rate declaration is a handheld tier thing");
        System.out.println("profile selection checks passed; runtime rendering not covered");
    }
}
