package org.ffrs.diagnostic;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.graphics.Typeface;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;

/** Minimal JNI carrier that displays actual capability and module diagnostics. */
public final class DiagnosticActivity extends Activity implements View.OnClickListener, Runnable, Handler.Callback {
    static { System.loadLibrary("ffrs_diagnostic"); }
    private native String runNativeDiagnostic(String modulePath, String reportPath);
    private Button button;
    private TextView result;
    private Handler handler;
    private String module;
    private String report;

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
        result = new TextView(this);
        result.setTypeface(Typeface.MONOSPACE);
        result.setTextSize(12);
        result.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(result);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        setContentView(layout);
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
    }

    @Override public void onClick(View view) {
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
        if (!isFinishing() && !isDestroyed()) {
            result.setText((String) message.obj);
            button.setEnabled(true);
        }
        return true;
    }
}
