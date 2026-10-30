package org.ffrs.diagnostic;

/** Public Android API capabilities captured on the UI thread, not event/actuation results. */
public final class InputSnapshot {
    public String source = "InputDevice.getDeviceIds/getDevice/hasKeys/getMotionRanges";
    public String error = "";
    public boolean measured;
    public boolean enumeration_complete;
    public long captured_elapsed_realtime_ns;
    public Device[] devices = new Device[0];

    public static final class Device {
        public int id, vendor_id, product_id, controller_number, source_mask;
        public String name = "";
        public boolean is_virtual;
        public int[] key_codes = new int[0];
        public Axis[] axes = new Axis[0];
    }

    public static final class Axis {
        public int type, source_mask;
        public String name = "";
        public double min, max, flat, fuzz, resolution;
    }
}
