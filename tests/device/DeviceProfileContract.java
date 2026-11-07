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
        check(null, 0, null, "handheld_mid", 720, 30);
        check("SM8635", 768, "handheld_mid", "handheld_mid", 720, 30);
        check("MT6897", 420, "odin_elite", "odin_elite", 1080, 30);
        check("SM8635", 768, "invalid", "tablet_sd8s", 1080, 60);
        check("SM8635", 420, "", "handheld_mid", 720, 30);
        check("SM86350", 768, "", "handheld_mid", 720, 30);
        check("SM8635", 0, "", "handheld_mid", 720, 30);
        check("SM8635", 420, "tablet_sd8s", "tablet_sd8s", 1080, 60);
        check("SM8650", 420, "", "phone_sd8g3", 1080, 60);
        check("SM86500", 420, "", "handheld_mid", 720, 30);
        check("SM8650", 420, "handheld_mid", "handheld_mid", 720, 30);
        if (!DeviceProfile.select("SM8650", 420, "").usesDockedRendering()
                || DeviceProfile.select(null, 0, null).usesDockedRendering())
            throw new AssertionError("docked rendering selection");
        System.out.println("profile selection checks passed; runtime rendering not covered");
    }
}
