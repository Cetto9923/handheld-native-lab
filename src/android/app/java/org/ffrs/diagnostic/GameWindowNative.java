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
    /** Returns {present, pointer, width, height, format, generation}. */
    public static native long[] setSurface(Surface surface);
    /** Releases the published platform window reference. */
    public static native void clearSurface();
    /** Returns {present, pointer, width, height, format, generation}. */
    public static native long[] describeWindow();
}
