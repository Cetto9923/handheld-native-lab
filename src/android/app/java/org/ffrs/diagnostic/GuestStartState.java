package org.ffrs.diagnostic;

/** Process-lifetime startup state shared by successive Activity instances. */
public final class GuestStartState {
    public static final int IDLE = 0, STARTING = 1, STARTED = 2, FAILED = 3;
    private int state = IDLE;

    /** Only one caller may reserve an idle startup. */
    public synchronized boolean begin() {
        if (state != IDLE) return false;
        state = STARTING;
        return true;
    }

    /** The caller that reserved startup publishes the native result. */
    public synchronized void finish(boolean started) {
        if (state != STARTING) throw new IllegalStateException("No guest startup is pending");
        state = started ? STARTED : FAILED;
    }

    /** A retry is always explicit and can only clear a recorded failure. */
    public synchronized boolean retry() {
        if (state != FAILED) return false;
        state = IDLE;
        return true;
    }

    public synchronized int get() { return state; }
}
