package org.ffrs.diagnostic;

import android.app.Activity;
import android.graphics.Insets;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * Game window form only.
 *
 * The Activity owns a SurfaceView and hands the platform window of that
 * SurfaceView to the native holder. Nothing is rendered into it, no input is
 * consumed and no guest module runs here: this Activity exists so the runtime
 * can obtain a real, presentable window. The measured window geometry is
 * logged next to the geometry the native side reads back from the window, so
 * both can be compared against the existing display snapshot parameters.
 */
public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "FFRS-GameWindow";
    private SurfaceView surface_view;
    private DisplaySnapshot display_snapshot;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        configureWindow();
        surface_view = new SurfaceView(this);
        // RGBA_8888 is the buffer format a Vulkan surface can be created from.
        surface_view.getHolder().setFormat(PixelFormat.RGBA_8888);
        setContentView(surface_view);
        surface_view.getHolder().addCallback(this);
    }

    /** Full screen window so the SurfaceView covers the whole display. */
    private void configureWindow() {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // Touch the decor view first: the window insets controller only exists
        // once the decor view is installed and attached.
        View decor = window.getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
        } else {
            window.setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                    WindowManager.LayoutParams.FLAG_FULLSCREEN);
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) return;
        Window window = getWindow();
        View decor = window.getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller == null) controller = window.getInsetsController();
            if (controller == null) {
                Log.w(TAG, "window insets controller is not available yet");
                return;
            }
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else {
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            Log.e(TAG, "surface_created without a valid Surface; no window is published");
            return;
        }
        display_snapshot = captureDisplaySnapshot();
        long[] window = GameWindowNative.setSurface(surface);
        if (window == null) {
            Log.e(TAG, "surface_created: native window call returned null");
            return;
        }
        DisplaySnapshot snapshot = display_snapshot;
        Log.i(TAG, "window_form"
                + " display_width_px=" + snapshot.width_px
                + " display_height_px=" + snapshot.height_px
                + " content_width_px=" + snapshot.content_width_px
                + " content_height_px=" + snapshot.content_height_px
                + " surface_width_px=" + surface_view.getWidth()
                + " surface_height_px=" + surface_view.getHeight()
                + " inset_left_px=" + snapshot.inset_left_px
                + " inset_top_px=" + snapshot.inset_top_px
                + " inset_right_px=" + snapshot.inset_right_px
                + " inset_bottom_px=" + snapshot.inset_bottom_px
                + " size_source=" + snapshot.size_source
                + " insets_source=" + snapshot.insets_source
                + " density_dpi=" + snapshot.density_dpi
                + " refresh_rate_hz=" + snapshot.refresh_rate_hz
                + " rotation=" + snapshot.rotation
                + " native_present=" + window[0]
                + " native_pointer=0x" + Long.toHexString(window[1])
                + " native_width_px=" + window[2]
                + " native_height_px=" + window[3]
                + " native_format=" + window[4]
                + " native_generation=" + window[5]
                + " error=" + snapshot.error);
        // The Vulkan presentation probe runs off the UI thread: the driver
        // calls must not happen on the thread that owns the SurfaceView.
        Thread probe = new Thread(new VulkanProbeTask(), "ffrs-vk-probe");
        probe.start();
    }

    /** A named task, so the dexer sees a plain class and not an anonymous one. */
    private static final class VulkanProbeTask implements Runnable {
        @Override public void run() {
            int rc = GameWindowNative.nativeRunVulkanProbe();
            Log.i(TAG, "vulkan_probe rc=" + rc + " (host probe, not a game frame)");
        }
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surface_changed format=" + format
                + " width_px=" + width + " height_px=" + height);
        long[] window = GameWindowNative.describeWindow();
        if (window != null) {
            Log.i(TAG, "window_readback present=" + window[0]
                    + " pointer=0x" + Long.toHexString(window[1])
                    + " width_px=" + window[2] + " height_px=" + window[3]
                    + " format=" + window[4] + " generation=" + window[5]);
        }
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        GameWindowNative.clearSurface();
        Log.i(TAG, "surface_destroyed native_window_cleared");
    }

    @Override protected void onDestroy() {
        // Safety net: the runtime must never keep a window of a dead Activity.
        GameWindowNative.clearSurface();
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    private DisplaySnapshot captureDisplaySnapshot() {
        // Measured on the UI thread with the same parameters the diagnostic
        // carrier already uses, so the two can be compared directly.
        DisplaySnapshot snapshot = new DisplaySnapshot();
        snapshot.captured_elapsed_realtime_ns = SystemClock.elapsedRealtimeNanos();
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        snapshot.density_dpi = metrics.densityDpi;
        snapshot.density = metrics.density;
        View content = findViewById(android.R.id.content);
        if (content != null) {
            snapshot.content_width_px = content.getWidth();
            snapshot.content_height_px = content.getHeight();
        }
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                android.view.WindowMetrics window = getWindowManager().getCurrentWindowMetrics();
                Rect bounds = window.getBounds();
                snapshot.width_px = bounds.width();
                snapshot.height_px = bounds.height();
                snapshot.size_source = "WindowManager.currentWindowMetrics.bounds";
                Insets insets = window.getWindowInsets().getInsetsIgnoringVisibility(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                snapshot.inset_left_px = insets.left;
                snapshot.inset_top_px = insets.top;
                snapshot.inset_right_px = insets.right;
                snapshot.inset_bottom_px = insets.bottom;
                snapshot.insets_source = DisplaySnapshot.MODERN_INSETS_SOURCE;
                snapshot.insets_measured = true;
            } else {
                View decor = getWindow().getDecorView();
                snapshot.width_px = decor.getWidth();
                snapshot.height_px = decor.getHeight();
                snapshot.size_source = "Window.decorView.measured_bounds";
            }
        } catch (RuntimeException failure) {
            snapshot.error = "Window measurement: " + failure.toString();
        }
        try {
            Display display = Build.VERSION.SDK_INT >= 30
                    ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (display != null) {
                snapshot.display_id = display.getDisplayId();
                snapshot.rotation = display.getRotation();
                snapshot.refresh_rate_hz = display.getRefreshRate();
                snapshot.current_mode_id = display.getMode().getModeId();
            }
        } catch (RuntimeException failure) {
            snapshot.error += " Display measurement: " + failure.toString();
        }
        return snapshot;
    }
}
