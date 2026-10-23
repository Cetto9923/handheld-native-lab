package org.ffrs.diagnostic;

/** Host JVM fixture only: supplied measurements are synthetic, never device results. */
public final class DisplaySnapshotJniTest {
    static { System.loadLibrary("ffrs_display_test"); }
    private static native String inspect(Object snapshot);

    private static DisplaySnapshot snapshot(int width, int height) {
        DisplaySnapshot s = new DisplaySnapshot();
        s.width_px = width; s.height_px = height;
        s.content_width_px = width; s.content_height_px = height - 156;
        s.size_source = "WindowManager.currentWindowMetrics.bounds";
        s.density_dpi = 440; s.density = 2.75; s.refresh_rate_hz = 59.94;
        s.insets_measured = true; s.inset_top_px = 66; s.inset_bottom_px = 90;
        s.display_id = 0; s.rotation = 0; s.current_mode_id = 1;
        s.captured_elapsed_realtime_ns = 123456789;
        s.mode_ids = new int[] {1, 2};
        s.mode_widths = new int[] {1080, 1080};
        s.mode_heights = new int[] {2340, 2340};
        s.mode_refresh_rates = new double[] {60, 120};
        return s;
    }
    private static void emit(String name, Object snapshot) {
        System.out.println("{\"case\":\"" + name + "\",\"report\":" + inspect(snapshot) + "}");
    }
    public static void main(String[] args) {
        emit("portrait", snapshot(1080, 2340));
        DisplaySnapshot s = snapshot(2340, 1080); s.rotation = 1;
        emit("landscape", s);
        s = snapshot(3000, 1920); s.refresh_rate_hz = 144;
        s.mode_widths = new int[] {3000, 3000}; s.mode_heights = new int[] {1920, 1920};
        emit("tablet_synthetic", s);
        s.width_px = 1500; s.height_px = 900; s.content_width_px = 1500; s.content_height_px = 744;
        emit("multiwindow", s);
        s = snapshot(1080, 2340); s.refresh_rate_hz = Double.NaN; emit("unknown_refresh", s);
        s = snapshot(1080, 2340); s.density = Double.POSITIVE_INFINITY; emit("unknown_density", s);
        s = snapshot(0, 2340); emit("missing_size", s);
        s = snapshot(1080, 2340); s.mode_widths = new int[] {1080}; emit("bad_mode_arrays", s);
        s = snapshot(1080, 2340);
        s.mode_ids = new int[0]; s.mode_widths = new int[0]; s.mode_heights = new int[0]; s.mode_refresh_rates = new double[0];
        emit("no_modes", s);
        emit("no_snapshot", null);
        emit("wrong_object", new Object());
    }
}
