package org.ffrs.diagnostic;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.os.SystemClock;
import android.graphics.Typeface;
import android.view.View;
import android.view.KeyEvent;
import android.view.MotionEvent;
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
import org.json.JSONObject;

/** Minimal JNI carrier that displays actual capability and module diagnostics. */
public final class DiagnosticActivity extends Activity implements View.OnClickListener, Runnable, Handler.Callback {
    static { System.loadLibrary("ffrs_diagnostic"); }
    private native String runNativeDiagnostic(String modulePath, String reportPath);
    private Button button;
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
        result.setText("No game module is included.\nAuthorized uncompressed NSO path:\n" + module
                + "\n\nReport path:\n" + report + "\n\nThis carrier only loads and relocates; it does not execute game code.");
        button.setOnClickListener(this);
        button.requestFocus();
    }

    @Override public void onClick(View view) {
        recordEvent("native_diagnostic_start", new JSONObject());
        button.setEnabled(false);
        result.setText("Running diagnostics...\nGame code execution: NOT_RUN");
        new Thread(this, "ffrs-diagnostic").start();
    }

    @Override public void run() {
        String diagnostics;
        try { diagnostics = runNativeDiagnostic(module, report); }
        catch (Throwable failure) { diagnostics = "JNI_FAILED: " + failure.toString(); }
        handler.obtainMessage(1, diagnostics).sendToTarget();
    }

    @Override public boolean handleMessage(Message message) {
        recordEvent("native_diagnostic_finish", new JSONObject());
        if (!isFinishing() && !isDestroyed()) {
            result.setText((String) message.obj);
            button.setEnabled(true);
        }
        return true;
    }

    @Override public void onResume() {
        super.onResume();
        recordEvent("onResume", new JSONObject());
    }

    @Override public void onPause() {
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
