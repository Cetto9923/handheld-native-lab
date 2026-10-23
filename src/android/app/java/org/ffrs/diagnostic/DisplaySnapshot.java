package org.ffrs.diagnostic;

/** UI-thread measurement handed to native code once, without retaining Activity/JVM references. */
public final class DisplaySnapshot {
    public static final String MODERN_INSETS_SOURCE =
            "WindowInsets.getInsetsIgnoringVisibility(systemBars|displayCutout)";
    public static final String LEGACY_INSETS_SOURCE =
            "max(WindowInsets.stableInsets,DisplayCutout.safeInsets)";
    public String size_source = "";
    public String error = "";
    public String insets_source = "";
    public int width_px, height_px, content_width_px, content_height_px;
    public int inset_left_px, inset_top_px, inset_right_px, inset_bottom_px;
    public boolean insets_measured;
    public int display_id = -1;
    public int rotation = -1;
    public int current_mode_id;
    public int density_dpi;
    public double density, refresh_rate_hz;
    public long captured_elapsed_realtime_ns;
    public int[] mode_ids = new int[0];
    public int[] mode_widths = new int[0];
    public int[] mode_heights = new int[0];
    public double[] mode_refresh_rates = new double[0];

    /** API28/29 system-bar/cutout geometry; no visibility-sensitive system/IME insets. */
    public void setStableInsetsWithCutout(int left, int top, int right, int bottom,
            int cutoutLeft, int cutoutTop, int cutoutRight, int cutoutBottom) {
        insets_measured = false;
        insets_source = "";
        inset_left_px = inset_top_px = inset_right_px = inset_bottom_px = 0;
        if (left < 0 || top < 0 || right < 0 || bottom < 0
                || cutoutLeft < 0 || cutoutTop < 0 || cutoutRight < 0 || cutoutBottom < 0) return;
        inset_left_px = Math.max(left, cutoutLeft);
        inset_top_px = Math.max(top, cutoutTop);
        inset_right_px = Math.max(right, cutoutRight);
        inset_bottom_px = Math.max(bottom, cutoutBottom);
        insets_source = LEGACY_INSETS_SOURCE;
        insets_measured = true;
    }
}
