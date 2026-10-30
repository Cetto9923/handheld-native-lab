package org.ffrs.diagnostic;

import android.view.Surface;

/**
 * Native entry points for the game window form.
 *
 * setSurface publishes the platform window that belongs to a Java Surface. The
 * native side obtains it through ANativeWindow_fromSurface, so the Surface
 * object stays the single source of truth and no window pointer is ever
 * guessed on the Java side. clearSurface releases that reference and leaves
 * the runtime without a window. describeWindow reads the published window
 * back without touching a Java object.
 */
public final class GameWindowNative {
    static { System.loadLibrary("ffrs_game_window"); }
    private GameWindowNative() {}
    /** Actual Activity focus: 1 focused, 2 unfocused, 3 background. */
    public static native void setFocusState(int state);
    public static native void setPadState(boolean connected, long buttons, int lx, int ly, int rx, int ry);
    public static native void audioDevicesChanged();
    public static native boolean hostExitReady(boolean request);
    /** Returns {present, pointer, width, height, format, generation}. */
    public static native long[] setSurface(Surface surface);
    /** Releases the published platform window reference. */
    public static native void clearSurface();
    /** Returns {present, pointer, width, height, format, generation}. */
    public static native long[] describeWindow();
    /**
     * Runs the Vulkan presentation host probe against the published window:
     * a surface from this Activity's own window, a swapchain, a clear, a
     * submit, a real fence wait and a present.
     *
     * Returns 0 when the clear reached the screen. This is a host probe and
     * never a game frame: the colour it draws is the probe's own, nothing
     * comes from the guest.
     */
    public static native int nativeRunVulkanProbe();
    /**
     * Starts the owned guest inside this process, on its own thread.
     *
     * The guest image is loaded and booted exactly like the standalone CLI
     * does it. The guest blocks for the whole session, so the caller must not
     * run this on the UI thread. The guest transcript is written to
     * logPath, because it is far too long for the log buffer.
     *
     * Returns the guest thread id, or -1 when the guest was refused.
     */
    public static native long nativeStartGuest(String image, String romfs, String logPath,
            long quota, long stack, long resources, String[] guestArgs);
}
