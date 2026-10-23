package org.ffrs.diagnostic;

/** Synthetic host-only fixture: no Android service, game code or input injection. */
public final class InputSnapshotJniTest {
    static { System.loadLibrary("ffrs_input_test"); }
    private static native String inspect(Object snapshot);
    private static InputSnapshot snapshot() {
        InputSnapshot s = new InputSnapshot();
        s.measured = true; s.enumeration_complete = true;
        s.captured_elapsed_realtime_ns = 123456789;
        return s;
    }
    private static InputSnapshot.Device device(int id, String name, int source) {
        InputSnapshot.Device d = new InputSnapshot.Device();
        d.id = id; d.name = name; d.source_mask = source;
        d.vendor_id = 0x1234; d.product_id = 0xabcd; d.controller_number = 1;
        return d;
    }
    private static InputSnapshot gamepad() {
        InputSnapshot s = snapshot();
        InputSnapshot.Device d = device(5, "Unbranded input", 0x01000010);
        InputSnapshot.Axis x = new InputSnapshot.Axis();
        x.type = 0; x.name = "AXIS_X"; x.source_mask = 0x01000010;
        x.min = -1; x.max = 1; x.flat = 0.0625; x.fuzz = 0.03125; x.resolution = 0.5;
        d.axes = new InputSnapshot.Axis[] {x};
        s.devices = new InputSnapshot.Device[] {d};
        return s;
    }
    private static void emit(String name, Object snapshot) {
        System.out.println("{\"case\":\"" + name + "\",\"report\":" + inspect(snapshot) + "}");
    }
    public static void main(String[] args) {
        emit("joystick", gamepad());
        InputSnapshot s = gamepad();
        InputSnapshot.Device buttons = device(6, "Buttons", 0x501);
        buttons.key_codes = new int[] {4, 96, 97, 108};
        InputSnapshot.Device keyboard = device(-1, "Anbernic Gamepad Controller Joystick", 0x101);
        keyboard.is_virtual = true;
        InputSnapshot.Device touch = device(8, "Touchscreen", 0x1002);
        touch.axes = s.devices[0].axes;
        s.devices = new InputSnapshot.Device[] {s.devices[0], buttons, keyboard, touch};
        emit("multiple_sources", s);
        emit("measured_empty", snapshot());
        s = gamepad(); s.enumeration_complete = false; s.error = "Device 6 detached during capture";
        emit("partial_capture", s);
        s = gamepad(); s.measured = false; s.error = "Enumeration service unavailable";
        emit("not_measured", s);
        s = gamepad(); s.source = ""; emit("missing_source", s);
        s = gamepad(); s.devices = new InputSnapshot.Device[] {s.devices[0], s.devices[0]};
        emit("duplicate_id", s);
        s = gamepad(); s.devices[0].axes[0].min = Double.NaN; emit("nan_axis", s);
        s = gamepad(); s.devices[0].axes[0].max = -2; emit("reversed_axis", s);
        s = gamepad(); s.devices[0].key_codes = new int[] {-1}; emit("negative_key", s);
        s = snapshot(); s.devices = null; emit("null_devices", s);
        s = snapshot(); s.devices = new InputSnapshot.Device[] {null}; emit("null_device", s);
        s = gamepad(); s.devices[0].axes = null; emit("null_axes", s);
        s = gamepad(); s.devices[0].key_codes = null; emit("null_keys", s);
        s = snapshot(); s.devices = new InputSnapshot.Device[257]; emit("oversize_devices", s);
        s = snapshot(); s.devices = new InputSnapshot.Device[] {device(5, "Gamepad", 0x10)};
        emit("source_class_bits_only", s);
        emit("no_snapshot", null);
        emit("wrong_object", new Object());
    }
}
