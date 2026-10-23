package org.ffrs.diagnostic;

/** UI-thread measurement handed to native code once, without retaining Activity/JVM references. */
public final class DisplaySnapshot {
    public String size_source = "";
    public String error = "";
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
}
