package org.ffrs.diagnostic;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/** Standalone Java entry point. It can export a report before the native game starts. */
public final class ReportActivity extends Activity {
    private static final int CREATE_REPORT = 4710;
    private ExportState state;
    private TextView status;
    private Button export;

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Object retained = getLastNonConfigurationInstance();
        state = retained instanceof ExportState ? (ExportState)retained : new ExportState();
        state.owner = new WeakReference<ReportActivity>(this);
        setTitle("导出诊断报告");
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        TextView explanation = new TextView(this);
        explanation.setText("将本应用的设备信息、运行日志和系统退出记录保存为 ZIP 文件。请选择保存位置，完成后可把文件发送给开发者。\n\n报告大小最多 16 MiB，保存过程可能需要一些时间。");
        explanation.setTextSize(17);
        layout.addView(explanation);
        export = new Button(this);
        export.setText("选择位置并导出");
        export.setOnClickListener(new ExportClick(this));
        layout.addView(export);
        status = new TextView(this);
        status.setTextIsSelectable(true);
        layout.addView(status);
        setContentView(layout);
        updateUi();
    }

    @Override public Object onRetainNonConfigurationInstance() { return state; }

    @Override protected void onDestroy() {
        if (state.owner.get() == this) state.owner.clear();
        super.onDestroy();
    }

    private void updateUi() {
        status.setText(state.message);
        export.setEnabled(!state.busy && !state.choosing);
    }

    private void chooseDestination() {
        if (state.busy || state.choosing) return;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
        intent.putExtra(Intent.EXTRA_TITLE, "FFRS-report-" + stamp + ".zip");
        state.choosing = true;
        state.message = "请选择诊断报告的保存位置。";
        updateUi();
        try {
            startActivityForResult(intent, CREATE_REPORT);
        } catch (ActivityNotFoundException | SecurityException e) {
            state.choosing = false;
            state.message = "无法打开文件保存界面：" + describe(e) + "\n请重试。";
            updateUi();
        }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != CREATE_REPORT) return;
        state.choosing = false;
        if (resultCode != RESULT_OK) {
            state.message = "已取消保存，可以重新选择位置。";
            updateUi();
            return;
        }
        Uri target = data == null ? null : data.getData();
        if (target == null) {
            state.message = "未取得保存位置，请重试。";
            updateUi();
            return;
        }
        state.busy = true;
        state.message = "正在收集并保存诊断报告…";
        updateUi();
        new Thread(new ExportTask(getApplicationContext(), target, state), "ffrs-diagnostic-report").start();
    }

    private static final class ExportState {
        WeakReference<ReportActivity> owner = new WeakReference<ReportActivity>(null);
        boolean busy;
        boolean choosing;
        String message = "点击按钮开始导出。";
    }

    private static final class ExportClick implements View.OnClickListener {
        private final ReportActivity owner;
        ExportClick(ReportActivity owner) { this.owner = owner; }
        @Override public void onClick(View view) { owner.chooseDestination(); }
    }

    private static final class ExportTask implements Runnable {
        private final Context context;
        private final Uri target;
        private final ExportState state;

        ExportTask(Context context, Uri target, ExportState state) {
            this.context = context;
            this.target = target;
            this.state = state;
        }

        @Override public void run() {
            List<DiagnosticReport.Trace> traces = new ArrayList<DiagnosticReport.Trace>();
            DiagnosticReport.Result result;
            try {
                File external = context.getExternalFilesDir(null);
                String metadata = metadata(context, external);
                String exits = exitHistory(context, traces);
                OutputStream output = context.getContentResolver().openOutputStream(target, "wt");
                result = DiagnosticReport.write(output, metadata, exits, external,
                        new File(context.getFilesDir(), "runtime"), traces);
            } catch (Exception e) {
                result = new DiagnosticReport.Result(0, 0, describe(e));
            } finally {
                for (DiagnosticReport.Trace trace : traces) {
                    if (trace.input != null) {
                        try { trace.input.close(); } catch (IOException ignored) { }
                    }
                }
            }
            new Handler(Looper.getMainLooper()).post(new ExportFinished(state, result));
        }
    }

    private static final class ExportFinished implements Runnable {
        private final ExportState state;
        private final DiagnosticReport.Result result;
        ExportFinished(ExportState state, DiagnosticReport.Result result) {
            this.state = state;
            this.result = result;
        }
        @Override public void run() {
            state.busy = false;
            state.message = result.ok() ? result.describe() + "\n请在所选位置找到 ZIP 文件，并发送给开发者。"
                    : result.describe() + "\n本次保存的文件可能不完整，请重新选择位置并导出。";
            ReportActivity owner = state.owner.get();
            if (owner != null) owner.updateUi();
        }
    }

    static String apkSha256(File file) throws Exception {
        java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder value = new StringBuilder();
        for (byte element : digest.digest()) value.append(String.format(Locale.ROOT, "%02x", element & 255));
        return value.toString();
    }

    private static String metadata(Context context, File external) {
        StringBuilder text = new StringBuilder("FFRS diagnostic metadata\n");
        text.append("created_at=").append(new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSXXX", Locale.US).format(new Date()))
                .append("\ncreated_at_ms=").append(System.currentTimeMillis())
                .append("\npackage=").append(context.getPackageName())
                .append("\ndiagnostic_pid=").append(android.os.Process.myPid());
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            text.append("\nversion_name=").append(info.versionName)
                    .append("\nversion_code=").append(info.getLongVersionCode())
                    .append("\napp_last_update_time_ms=").append(info.lastUpdateTime);
        } catch (Exception e) { text.append("\napp_version=unavailable ").append(describe(e)); }
        try {
            String path = context.getApplicationInfo().sourceDir;
            PackageInfo archive = context.getPackageManager().getPackageArchiveInfo(path, 0);
            if (archive != null) {
                text.append("\nbase_apk_version_name=").append(archive.versionName)
                        .append("\nbase_apk_version_code=").append(archive.getLongVersionCode());
            }
            text.append("\nbase_apk_sha256=").append(apkSha256(new File(path)));
        } catch (Exception e) { text.append("\nbase_apk=unavailable ").append(describe(e)); }
        String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : Build.HARDWARE;
        text.append("\nmanufacturer=").append(Build.MANUFACTURER)
                .append("\nmodel=").append(Build.MODEL)
                .append("\nandroid_release=").append(Build.VERSION.RELEASE)
                .append("\nandroid_api=").append(Build.VERSION.SDK_INT)
                .append("\nsoc=").append(soc)
                .append("\nsoc_source=").append(Build.VERSION.SDK_INT >= 31 ? "SOC_MODEL" : "HARDWARE");
        ActivityManager manager = (ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
        try {
            if (manager == null) throw new IllegalStateException("ActivityManager unavailable");
            int mainPid = 0;
            List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) {
                for (ActivityManager.RunningAppProcessInfo process : processes) {
                    if (context.getPackageName().equals(process.processName)) mainPid = process.pid;
                }
            }
            text.append("\nmain_pid=").append(mainPid);
        } catch (Exception e) { text.append("\nmain_pid=unavailable ").append(describe(e)); }
        try {
            if (manager == null) throw new IllegalStateException("ActivityManager unavailable");
            ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
            manager.getMemoryInfo(memory);
            text.append("\nram_total_bytes=").append(memory.totalMem)
                    .append("\nram_available_bytes=").append(memory.availMem)
                    .append("\nlow_memory=").append(memory.lowMemory);
        } catch (Exception e) { text.append("\nram=unavailable ").append(describe(e)); }
        try {
            String override = profileOverride(external);
            int smallest = context.getResources().getConfiguration().smallestScreenWidthDp;
            android.view.WindowManager windows = (android.view.WindowManager)context.getSystemService(Context.WINDOW_SERVICE);
            Display display = windows == null ? null : windows.getDefaultDisplay();
            Display.Mode mode = display == null ? null : display.getMode();
            int width = DeviceProfile.formFactorWidthDp(mode == null ? 0 : mode.getPhysicalWidth(),
                    mode == null ? 0 : mode.getPhysicalHeight(), DisplayMetrics.DENSITY_DEVICE_STABLE, smallest);
            DeviceProfile profile = DeviceProfile.select(soc, Build.MODEL, width, override);
            text.append("\nprofile=").append(profile.id)
                    .append("\nprofile_source=current device rules and device-profile override")
                    .append("\nprofile_target_fps=").append(profile.targetFps)
                    .append("\nprofile_render_size=").append(profile.renderTierWidth()).append('x').append(profile.renderTierHeight());
        } catch (Exception e) { text.append("\nprofile=unavailable ").append(describe(e)); }
        return text.append('\n').toString();
    }

    private static String profileOverride(File external) throws IOException {
        if (external == null) return "";
        File source = new File(external.getCanonicalFile(), "device-profile");
        if (!source.isFile() || !source.getCanonicalFile().equals(source)) return "";
        byte[] buffer = new byte[128];
        try (FileInputStream input = new FileInputStream(source)) {
            int count = input.read(buffer);
            if (count <= 0) return "";
            String[] words = new String(buffer, 0, count, StandardCharsets.UTF_8).trim().split("\\s+", 2);
            String word = words[0];
            return word.matches("[A-Za-z0-9_]{1,32}") ? word : "";
        }
    }

    private static String exitHistory(Context context, List<DiagnosticReport.Trace> traces) {
        if (Build.VERSION.SDK_INT < 30) return "unavailable: historical exit reasons require Android API 30 or newer\n";
        return ExitHistoryApi30.collect(context, traces);
    }

    private static final class ExitHistoryApi30 {
        static String collect(Context context, List<DiagnosticReport.Trace> traces) {
            try {
                ActivityManager manager = (ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
                if (manager == null) return "unavailable: ActivityManager unavailable\n";
                List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 8);
                if (exits == null || exits.isEmpty()) return "unavailable: no historical exit records returned for this package\n";
                StringBuilder text = new StringBuilder("package=" + context.getPackageName() + "\n");
                for (int i = 0; i < Math.min(8, exits.size()); i++) {
                    ApplicationExitInfo exit = exits.get(i);
                    String description = exit.getDescription();
                    if (description != null && description.length() > 4096) description = description.substring(0, 4096) + " [truncated]";
                    text.append("\nexit=").append(i + 1).append("\nreason=").append(exit.getReason())
                            .append("\nstatus=").append(exit.getStatus()).append("\ntimestamp_ms=").append(exit.getTimestamp())
                            .append("\npid=").append(exit.getPid()).append("\nprocess=").append(exit.getProcessName())
                            .append("\nimportance=").append(exit.getImportance()).append("\npss_kib=").append(exit.getPss())
                            .append("\nrss_kib=").append(exit.getRss()).append("\ndescription=").append(description).append('\n');
                    traces.add(Build.VERSION.SDK_INT >= 31 ? TraceApi31.open(exit)
                            : new DiagnosticReport.Trace(null, "system trace collection requires Android API 31 or newer"));
                }
                return text.toString();
            } catch (Exception e) { return "unavailable: " + describe(e) + "\n"; }
        }
    }

    private static final class TraceApi31 {
        static DiagnosticReport.Trace open(ApplicationExitInfo exit) {
            try {
                InputStream input = exit.getTraceInputStream();
                return new DiagnosticReport.Trace(input, input == null ? "system returned no trace for this exit" : null);
            } catch (Exception e) { return new DiagnosticReport.Trace(null, describe(e)); }
        }
    }

    private static String describe(Exception error) {
        String message = error.getMessage();
        return error.getClass().getSimpleName() + (message == null ? "" : " " + message);
    }
}
