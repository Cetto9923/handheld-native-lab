package org.ffrs.diagnostic;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.SystemClock;
import android.graphics.Typeface;
import android.graphics.Rect;
import android.graphics.Insets;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.WindowMetrics;
import android.view.WindowInsets;
import android.view.View;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputDevice;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONObject;

/** Minimal JNI carrier that displays actual capability and module diagnostics. */
public final class DiagnosticActivity extends Activity implements View.OnClickListener, Runnable, Handler.Callback {
    static { System.loadLibrary("ffrs_diagnostic"); }
    private native String runNativeDiagnostic(String modulePath, String reportPath, DisplaySnapshot display, InputSnapshot input);
    private native String runNativeLeafDiagnostic(String libraryPath, String reportPath);
    private Button button;
    private Button leafButton;
    private Button audioButton;
    private boolean leafMode;
    private boolean audioMode;
    private Thread diagnosticWorker;
    private File audioReport;
    private boolean hardwareReady;
    private DisplaySnapshot displaySnapshot;
    private InputSnapshot inputSnapshot;
    private String leafLibrary;
    private String leafReport;
    private TextView result;
    private Handler handler;
    private String module;
    private String report;
    private TextView eventStatus;
    private File eventFile;
    private int eventCount;
    private long lastMotionMs;
    private long skippedMotion;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        handler = new Handler(Looper.getMainLooper(), this);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 24, 24, 24);
        TextView title = new TextView(this);
        title.setText("FFRS Native Diagnostic\nGame startup and rendering: NOT_RUN");
        title.setTextSize(20);
        layout.addView(title);
        button = new Button(this);
        button.setText("Run hardware + NSO loader diagnostics");
        layout.addView(button);
        leafButton = new Button(this);
        leafButton.setText("Validate optional original leaf · 16,875 samples");
        layout.addView(leafButton);
        audioButton = new Button(this);
        audioButton.setText("Play 0.25s quiet audio probe · volume unchanged");
        layout.addView(audioButton);
        eventStatus = new TextView(this);
        eventStatus.setText("Hardware event capture: waiting. This tests the diagnostic host only.");
        layout.addView(eventStatus);
        result = new TextView(this);
        result.setTypeface(Typeface.MONOSPACE);
        result.setTextSize(12);
        result.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(result);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(layout);
        eventFile = new File(getFilesDir(), "hardware-events.jsonl");
        recordEvent("onCreate", new JSONObject());
        probePrivateStorage();
        leafLibrary = new File(getFilesDir(), "libffrs_leaf.so").getAbsolutePath();
        leafReport = new File(getFilesDir(), "leaf-report.json").getAbsolutePath();
        leafButton.setOnClickListener(this);
        audioReport = new File(getFilesDir(), "audio-probe.json");
        audioButton.setOnClickListener(this);
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        File modules = new File(root, "modules");
        if (!modules.isDirectory() && !modules.mkdirs()) {
            result.setText("BLOCKED_STORAGE: cannot create " + modules.getAbsolutePath());
            button.setEnabled(false);
            return;
        }
        module = new File(modules, "main.nso").getAbsolutePath();
        report = new File(root, "diagnostic.json").getAbsolutePath();
        hardwareReady = true;
        result.setText("No game module is included.\nAuthorized uncompressed NSO path:\n" + module
                + "\n\nReport path:\n" + report + "\n\nNSO mode only loads and relocates."
                + "\nOptional leaf library (generated locally, absent = NOT_RUN):\n" + leafLibrary
                + "\nLeaf source: main+0x694bd0, SHA256 8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9"
                + "\nPlanned leaf samples: 16,875. Full game startup and rendering remain NOT_RUN.");
        button.setOnClickListener(this);
        button.requestFocus();
    }

    @Override public void onClick(View view) {
        audioMode = view == audioButton;
        leafMode = view == leafButton;
        if (!leafMode && !audioMode) {
            displaySnapshot = captureDisplaySnapshot();
            inputSnapshot = captureInputSnapshot();
        }
        recordEvent(audioMode ? "explicit_audio_probe_start" : leafMode ? "leaf_diagnostic_start" : "native_diagnostic_start", new JSONObject());
        button.setEnabled(false);
        leafButton.setEnabled(false);
        audioButton.setEnabled(false);
        result.setText(audioMode ? "Playing a quiet 0.25s diagnostic tone. Audibility needs your confirmation."
                : leafMode ? "Checking optional leaf identity before any call..." : "Running hardware + NSO diagnostics...");
        diagnosticWorker = new Thread(this, "ffrs-diagnostic");
        diagnosticWorker.start();
    }

    @Override public void run() {
        String diagnostics;
        try {
            if (audioMode) {
                AudioProbeResult audio = AndroidAudioProbe.run(getApplicationContext());
                diagnostics = AndroidAudioProbe.toJson(audio);
                // Serialize once: the successful file payload and UI payload are identical.
                try (FileOutputStream output = new FileOutputStream(audioReport)) {
                    output.write(diagnostics.getBytes(StandardCharsets.UTF_8));
                    output.getFD().sync();
                }
            } else diagnostics = leafMode ? runNativeLeafDiagnostic(leafLibrary, leafReport)
                    : runNativeDiagnostic(module, report, displaySnapshot, inputSnapshot);
        } catch (Throwable failure) { diagnostics = (audioMode ? "AUDIO_PROBE_FAILED: " : "JNI_FAILED: ") + failure.toString(); }
        handler.obtainMessage(1, diagnostics).sendToTarget();
    }

    @Override public boolean handleMessage(Message message) {
        recordEvent(audioMode ? "explicit_audio_probe_finish" : leafMode ? "leaf_diagnostic_finish" : "native_diagnostic_finish", new JSONObject());
        if (!isFinishing() && !isDestroyed()) {
            result.setText((String) message.obj);
            button.setEnabled(hardwareReady);
            leafButton.setEnabled(true);
            audioButton.setEnabled(true);
        }
        return true;
    }

    @SuppressWarnings("deprecation")
    private DisplaySnapshot captureDisplaySnapshot() {
        // Called by onClick on the UI thread. No window API is queried by the JNI worker.
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
                WindowMetrics window = getWindowManager().getCurrentWindowMetrics();
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
                if (snapshot.width_px <= 0 || snapshot.height_px <= 0) {
                    DisplayMetrics app = new DisplayMetrics();
                    getWindowManager().getDefaultDisplay().getMetrics(app);
                    snapshot.width_px = app.widthPixels;
                    snapshot.height_px = app.heightPixels;
                    snapshot.size_source = "Display.getMetrics.app_bounds";
                }
                WindowInsets insets = decor.getRootWindowInsets();
                if (insets != null && !insets.isConsumed()) {
                    DisplayCutout cutout = insets.getDisplayCutout();
                    snapshot.setStableInsetsWithCutout(insets.getStableInsetLeft(), insets.getStableInsetTop(),
                            insets.getStableInsetRight(), insets.getStableInsetBottom(),
                            cutout == null ? 0 : cutout.getSafeInsetLeft(),
                            cutout == null ? 0 : cutout.getSafeInsetTop(),
                            cutout == null ? 0 : cutout.getSafeInsetRight(),
                            cutout == null ? 0 : cutout.getSafeInsetBottom());
                }
            }
        } catch (RuntimeException failure) { snapshot.error = "Window measurement: " + failure.toString(); }
        try {
            Display display = Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (display != null) {
                snapshot.display_id = display.getDisplayId();
                snapshot.rotation = display.getRotation();
                snapshot.refresh_rate_hz = display.getRefreshRate();
                snapshot.current_mode_id = display.getMode().getModeId();
                Display.Mode[] modes = display.getSupportedModes();
                snapshot.mode_ids = new int[modes.length];
                snapshot.mode_widths = new int[modes.length];
                snapshot.mode_heights = new int[modes.length];
                snapshot.mode_refresh_rates = new double[modes.length];
                for (int i = 0; i < modes.length; i++) {
                    snapshot.mode_ids[i] = modes[i].getModeId();
                    snapshot.mode_widths[i] = modes[i].getPhysicalWidth();
                    snapshot.mode_heights[i] = modes[i].getPhysicalHeight();
                    snapshot.mode_refresh_rates[i] = modes[i].getRefreshRate();
                }
            }
        } catch (RuntimeException failure) { snapshot.error += " Display measurement: " + failure.toString(); }
        return snapshot;
    }

    private InputSnapshot captureInputSnapshot() {
        // Capabilities only. Existing event listeners still forward all events to Android.
        InputSnapshot snapshot = new InputSnapshot();
        snapshot.captured_elapsed_realtime_ns = SystemClock.elapsedRealtimeNanos();
        ArrayList<InputSnapshot.Device> devices = new ArrayList<>();
        try {
            int[] ids = InputDevice.getDeviceIds();
            int[] keys = new int[KeyEvent.getMaxKeyCode() + 1];
            for (int key = 0; key < keys.length; key++) keys[key] = key;
            snapshot.measured = true;
            snapshot.enumeration_complete = true;
            for (int id : ids) {
                try {
                    InputDevice device = InputDevice.getDevice(id);
                    if (device == null) throw new IllegalStateException("Device detached during capture: " + id);
                    InputSnapshot.Device d = new InputSnapshot.Device();
                    d.id = device.getId(); d.name = device.getName();
                    d.vendor_id = device.getVendorId(); d.product_id = device.getProductId();
                    d.controller_number = device.getControllerNumber();
                    d.source_mask = device.getSources(); d.is_virtual = device.isVirtual();
                    boolean[] supported = device.hasKeys(keys);
                    if (supported.length != keys.length) throw new IllegalStateException("hasKeys length mismatch");
                    int count = 0;
                    for (boolean present : supported) if (present) count++;
                    d.key_codes = new int[count];
                    for (int key = 0, n = 0; key < keys.length; key++) if (supported[key]) d.key_codes[n++] = key;
                    List<InputDevice.MotionRange> ranges = device.getMotionRanges();
                    d.axes = new InputSnapshot.Axis[ranges.size()];
                    for (int i = 0; i < ranges.size(); i++) {
                        InputDevice.MotionRange range = ranges.get(i);
                        InputSnapshot.Axis axis = new InputSnapshot.Axis();
                        axis.type = range.getAxis(); axis.name = MotionEvent.axisToString(axis.type);
                        axis.source_mask = range.getSource();
                        axis.min = range.getMin(); axis.max = range.getMax();
                        axis.flat = range.getFlat(); axis.fuzz = range.getFuzz(); axis.resolution = range.getResolution();
                        d.axes[i] = axis;
                    }
                    devices.add(d);
                } catch (RuntimeException failure) {
                    snapshot.enumeration_complete = false;
                    snapshot.error += " Input device " + id + ": " + failure.toString();
                }
            }
        } catch (RuntimeException failure) {
            snapshot.error += " Input enumeration: " + failure.toString();
            snapshot.enumeration_complete = false;
        }
        snapshot.devices = devices.toArray(new InputSnapshot.Device[0]);
        return snapshot;
    }

    @Override public void onResume() {
        super.onResume();
        recordEvent("onResume", new JSONObject());
    }

    @Override public void onPause() {
        // Cancel only the explicitly requested tone when leaving the Activity.
        if (audioMode && diagnosticWorker != null) diagnosticWorker.interrupt();
        recordEvent("onPause", new JSONObject());
        super.onPause();
    }

    @Override public void onDestroy() {
        recordEvent("onDestroy", new JSONObject());
        super.onDestroy();
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        JSONObject details = new JSONObject();
        try {
            details.put("device_id", event.getDeviceId());
            details.put("source", event.getSource());
            details.put("action", event.getAction());
            details.put("keycode", event.getKeyCode());
            details.put("key_name", KeyEvent.keyCodeToString(event.getKeyCode()));
            details.put("scancode", event.getScanCode());
            details.put("repeat", event.getRepeatCount());
            details.put("event_time_ms", event.getEventTime());
        } catch (Exception failure) { Log.e("FFRS-Hardware", "Cannot encode key event", failure); }
        recordEvent("key", details);
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        long now = SystemClock.uptimeMillis();
        if (now - lastMotionMs >= 50) {
            JSONObject details = new JSONObject();
            try {
                details.put("device_id", event.getDeviceId());
                details.put("source", event.getSource());
                details.put("action", event.getActionMasked());
                details.put("event_time_ms", event.getEventTime());
                details.put("sample_period_ms", 50);
                details.put("skipped_motion_events", skippedMotion);
                int[] axes = {MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z,
                    MotionEvent.AXIS_RX, MotionEvent.AXIS_RY, MotionEvent.AXIS_RZ,
                    MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y,
                    MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER,
                    MotionEvent.AXIS_BRAKE, MotionEvent.AXIS_GAS};
                for (int axis : axes) details.put(MotionEvent.axisToString(axis), event.getAxisValue(axis));
            } catch (Exception failure) { Log.e("FFRS-Hardware", "Cannot encode motion event", failure); }
            lastMotionMs = now;
            skippedMotion = 0;
            recordEvent("motion", details);
        } else { skippedMotion++; }
        return super.dispatchGenericMotionEvent(event);
    }

    private void recordEvent(String kind, JSONObject details) {
        if (eventFile == null) return;
        try {
            details.put("kind", kind);
            details.put("scope", "diagnostic_host_only");
            details.put("elapsed_realtime_ns", SystemClock.elapsedRealtimeNanos());
            Log.i("FFRS-Hardware", details.toString());
            if (eventFile.length() < 1024 * 1024) {
                try (FileOutputStream output = new FileOutputStream(eventFile, true)) {
                    output.write((details.toString() + "\n").getBytes(StandardCharsets.UTF_8));
                }
                eventCount++;
                if (eventStatus != null) eventStatus.setText("Host events: " + eventCount + " / latest: " + kind
                    + "\nPrivate log: " + eventFile.getAbsolutePath());
            } else if (eventStatus != null) {
                eventStatus.setText("Private event log reached 1 MiB; existing evidence retained. Logcat continues.");
            }
        } catch (Exception failure) {
            Log.e("FFRS-Hardware", "Event log write failed", failure);
            if (eventStatus != null) eventStatus.setText("EVENT_LOG_FAILED: " + failure.toString());
        }
    }

    private void probePrivateStorage() {
        JSONObject details = new JSONObject();
        File sample = null;
        try {
            sample = File.createTempFile("ffrs-storage-", ".probe", getFilesDir());
            byte[] expected = ("FFRS_PRIVATE_STORAGE_V1:" + SystemClock.elapsedRealtimeNanos()).getBytes(StandardCharsets.UTF_8);
            try (FileOutputStream output = new FileOutputStream(sample)) {
                output.write(expected);
                output.getFD().sync();
            }
            ByteArrayOutputStream actual = new ByteArrayOutputStream();
            try (FileInputStream input = new FileInputStream(sample)) {
                byte[] buffer = new byte[128];
                int length;
                while ((length = input.read(buffer)) != -1) actual.write(buffer, 0, length);
            }
            details.put("status", Arrays.equals(expected, actual.toByteArray()) ? "PASS_FILE_LOOPBACK_ONLY" : "MISMATCH");
            details.put("path", sample.getAbsolutePath());
            details.put("bytes", actual.size());
            details.put("game_save_validation", "NOT_RUN");
        } catch (Exception failure) {
            try { details.put("status", "FILE_LOOPBACK_FAILED"); details.put("error", failure.toString()); }
            catch (Exception ignored) { Log.e("FFRS-Hardware", "Cannot encode storage failure", ignored); }
        } finally {
            if (sample != null) {
                try { details.put("temporary_file_removed", sample.delete()); }
                catch (Exception failure) { Log.e("FFRS-Hardware", "Cannot remove this probe's temporary file", failure); }
            }
        }
        recordEvent("private_storage_loopback", details);
    }
}
