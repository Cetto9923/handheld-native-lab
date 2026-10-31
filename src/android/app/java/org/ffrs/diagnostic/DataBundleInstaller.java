package org.ffrs.diagnostic;

import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipFile;

/** One process-owned import job. Activity recreation never starts a second writer. */
public final class DataBundleInstaller implements Runnable {
    public interface Listener { void onDataStateChanged(); }
    private static DataBundleInstaller instance;
    public static synchronized DataBundleInstaller get(Context context) {
        if (instance == null) instance = new DataBundleInstaller(context.getApplicationContext());
        return instance;
    }
    private final Context context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private Listener listener;
    public final boolean managed, directoryMode;
    public volatile File runtimeRoot;
    public volatile boolean busy;
    public volatile String message = "正在检查安装数据…";
    public volatile long done, total;
    public volatile File root;
    private volatile boolean cancel;
    private NativeDataBundle.Contract contract;
    private final ArrayList<File> stores = new ArrayList<File>();
    private int selected;
    private long lastProgress;

    private DataBundleInstaller(Context c) {
        context = c;
        boolean found = false;
        try { for (String name : c.getAssets().list("")) if (name.equals("data-contract.json")) found = true; }
        catch (IOException e) { found = true; }
        managed = found;
        boolean directory = false;
        if (found) {
            try (InputStream in = c.getAssets().open("data-contract.json")) {
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                byte[] buffer = new byte[4096]; int n;
                while ((n = in.read(buffer)) != -1) {
                    if (b.size() + n > 32768) break;
                    b.write(buffer, 0, n);
                }
                directory = "directory-v1".equals(new JSONObject(new String(b.toByteArray(), StandardCharsets.UTF_8)).optString("delivery"));
            } catch (Exception ignored) {}
        }
        directoryMode = directory;
        if (managed) { busy = true; new Thread(new Job(this, 0, null), "hnl-data-check").start(); }
        else message = "";
    }
    public void attach(Listener value) { listener = value; }
    public void detach(Listener value) { if (listener == value) listener = null; }
    @Override public void run() { if (listener != null) listener.onDataStateChanged(); }
    private void changed() { main.removeCallbacks(this); main.post(this); }

    private static String hash(InputStream input) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] b = new byte[128 * 1024]; int n;
        while ((n = input.read(b)) != -1) md.update(b, 0, n);
        StringBuilder s = new StringBuilder();
        for (byte v : md.digest()) s.append(String.format(Locale.ROOT, "%02x", v & 255));
        return s.toString();
    }
    private void initialize() throws Exception {
        if (!Arrays.asList(Build.SUPPORTED_ABIS).contains("arm64-v8a")) throw new IOException("此设备不支持 ARM64，本版本无法启动");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (InputStream in = context.getAssets().open("data-contract.json")) {
            byte[] b = new byte[4096]; int n;
            while ((n = in.read(b)) != -1) {
                if (bytes.size() + n > 32768) throw new IOException("APK 数据清单异常");
                bytes.write(b, 0, n);
            }
        }
        JSONObject spec = new JSONObject(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
        if (!"hnl-native-data/1".equals(spec.getString("format")) || !"arm64-v8a".equals(spec.getString("abi")))
            throw new IOException("APK 数据格式不受支持");
        total = spec.getLong("payload_bytes");
        if (total <= 0 || total > 64L * 1024 * 1024 * 1024) throw new IOException("APK 数据长度异常");
        HashMap<String, String> required = new HashMap<String, String>();
        JSONObject r = spec.getJSONObject("required");
        for (Iterator<String> keys = r.keys(); keys.hasNext();) { String k = keys.next(); required.put(k, r.getString(k)); }
        String nativeSha;
        try (ZipFile apk = new ZipFile(context.getApplicationInfo().sourceDir);
             InputStream in = apk.getInputStream(apk.getEntry("lib/arm64-v8a/libffrs_game_window.so"))) { nativeSha = hash(in); }
        contract = new NativeDataBundle.Contract(nativeSha, spec.getString("index_sha256"), required);
        if (directoryMode) { scanDirectory(); return; }
        for (File ext : context.getExternalFilesDirs(null)) {
            if (ext != null && Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState(ext)))
                stores.add(new File(ext, "hnl-data-v1"));
        }
        if (stores.isEmpty()) stores.add(new File(context.getFilesDir(), "hnl-data-v1"));
        long most = -1;
        String remembered = context.getSharedPreferences("hnl-install", 0).getString("store", "");
        for (int i = 0; i < stores.size(); i++) {
            File store = stores.get(i);
            if (!store.isDirectory() && !store.mkdirs()) continue;
            if (store.getUsableSpace() > most) { most = store.getUsableSpace(); selected = i; }
        }
        message = "请选择与 APK 同目录的 .hnldata 数据包。导入期间请保持本页在前台。\n可取消；重新选同一包可继续校验导入。";
        for (File store : stores) {
            if (!store.getAbsolutePath().equals(remembered)) continue;
            selected = stores.indexOf(store);
            File active = new File(store, "active.properties");
            if (!active.isFile()) break;
            Properties props = new Properties();
            try (InputStream in = new FileInputStream(active)) { props.load(in); }
            if (!NativeDataBundle.versionId(contract).equals(props.getProperty("bundle_id"))) {
                message = "当前 APK 与已选择的数据版本不匹配。可导入匹配数据包，或验证恢复本 APK 对应的旧版本。"; break;
            }
            NativeDataBundle.Installed data = NativeDataBundle.verify(store, contract, false, new Updates(this), new Stop(this));
            prepare(data.root); root = data.root;
            message = "数据检查完成，正在等待前台窗口启动。";
            break;
        }
    }
    private ArrayList<File> directoryCandidates() {
        LinkedHashSet<File> volumes = new LinkedHashSet<File>();
        volumes.add(Environment.getExternalStorageDirectory());
        if (Build.VERSION.SDK_INT >= 30) {
            android.os.storage.StorageManager sm = (android.os.storage.StorageManager)context.getSystemService(Context.STORAGE_SERVICE);
            if (sm != null) for (android.os.storage.StorageVolume v : sm.getStorageVolumes()) {
                File dir = v.getDirectory();
                if (dir != null && (Environment.MEDIA_MOUNTED.equals(v.getState())
                        || Environment.MEDIA_MOUNTED_READ_ONLY.equals(v.getState()))) volumes.add(dir);
            }
        } else {
            // Own app directories expose the physical volumes without hard-coded UUIDs.
            for (File ext : context.getExternalFilesDirs(null)) {
                if (ext == null) continue;
                File androidDir = ext.getParentFile().getParentFile().getParentFile();
                if ("Android".equals(androidDir.getName())) volumes.add(androidDir.getParentFile());
            }
        }
        ArrayList<File> result = new ArrayList<File>();
        String[] common = {"HandheldNativeLab/data", "Games/HandheldNativeLab/data", "Download/HandheldNativeLab/data"};
        for (File volume : volumes) for (String child : common) result.add(new File(volume, child));
        for (File ext : context.getExternalFilesDirs(null)) if (ext != null) result.add(new File(ext, "game"));
        return result;
    }
    public void rescanDirectory() {
        if (!directoryMode || busy || contract == null || root != null) return;
        busy = true; cancel = false; done = 0;
        message = "正在查找已拷贝的游戏目录…"; changed();
        new Thread(new Job(this, 3, null), "hnl-directory-check").start();
    }
    private void scanDirectory() throws Exception {
        ArrayList<File> paths = directoryCandidates();
        StringBuilder errors = new StringBuilder();
        android.content.SharedPreferences prefs = context.getSharedPreferences("hnl-install", 0);
        for (File path : paths) {
            if (!new File(path, "hnl-index.tsv").isFile()) continue;
            message = "正在校验游戏目录…\n" + path; changed();
            try {
                String key = path.getCanonicalPath() + "\n" + NativeDataBundle.versionId(contract);
                boolean verified = key.equals(prefs.getString("verified-directory", ""));
                NativeDataBundle.Installed data = NativeDataBundle.verifyDirectory(path, contract, !verified,
                        new Updates(this), new Stop(this));
                if (data.bytes != total) throw new IOException("APK 与目录总长度不匹配");
                prepare(data.root);
                if (!prefs.edit().putString("verified-directory", key).commit()) throw new IOException("无法保存目录检查结果");
                root = data.root;
                message = "目录校验完成，正在等待前台窗口启动。";
                Log.i("HNL-Installer", "directory verified root=" + root + " full_sha=" + !verified + " bytes=" + data.bytes);
                return;
            } catch (InterruptedIOException e) { throw e; }
            catch (IOException e) { errors.append(path).append(": ").append(e.getMessage()).append("\n"); }
        }
        boolean permission = Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
        if (errors.length() != 0) Log.w("HNL-Installer", "directory scan failures: " + errors);
        message = "请将整个 HandheldNativeLab 文件夹拷贝到手机或 SD 卡根目录，再打开本应用。\n"
                + "也支持 Games/HandheldNativeLab 或 Download/HandheldNativeLab。\n"
                + (permission ? "尚未找到完整匹配的数据。请检查 data 文件夹是否拷贝完整。" : "新版安卓需要首次允许文件访问；授权后自动重新查找。")
                + (errors.length() == 0 || !permission ? "" : "\n已发现目录但检查失败：\n" + errors);
    }
    private static String size(long value) { return String.format(Locale.ROOT, "%.2f GB", value / 1000000000.0); }
    public String storageText() {
        if (directoryMode) return "目录直接读取，不再复制一份数据。首次会校验文件，后续自动检查加载。\n存档独立保存在应用内，更新请覆盖安装；卸载前先备份存档。";
        if (stores.isEmpty()) return "";
        File store = stores.get(selected);
        boolean removable = false;
        try { removable = Environment.isExternalStorageRemovable(store); } catch (IllegalArgumentException ignored) {}
        return "导入位置：" + (removable ? "SD 卡" : "设备存储") + "（可切换）\n可用 " + size(store.getUsableSpace())
                + "；完整展开 " + size(total) + "，另保留约 " + size(Math.max(256L * 1024 * 1024, total / 20))
                + "\n更新会保留旧版本；存档单独保留。卸载应用会删除其专属数据和存档，请先备份。";
    }
    public void cycleStorage() {
        if (busy || stores.isEmpty()) return;
        selected = (selected + 1) % stores.size(); changed();
    }
    public void cancel() { cancel = true; message = "正在安全停止；旧版本和存档保留…"; changed(); }
    public void install(Uri uri) {
        if (busy || contract == null || stores.isEmpty()) return;
        busy = true; cancel = false; done = 0; root = null;
        message = "正在打开数据包…"; changed();
        new Thread(new Job(this, 1, uri), "hnl-data-import").start();
    }
    public void recover() {
        if (busy || contract == null || stores.isEmpty()) return;
        busy = true; cancel = false; done = 0; root = null;
        message = "正在完整校验本 APK 对应的已导入版本…"; changed();
        new Thread(new Job(this, 2, null), "hnl-data-recover").start();
    }
    private void complete(NativeDataBundle.Installed installed) throws IOException {
        if (installed.bytes != total) throw new IOException("APK 与数据总长度不匹配");
        prepare(installed.root);
        if (!context.getSharedPreferences("hnl-install", 0).edit().putString("store", installed.root.getParent()).commit())
            throw new IOException("无法保存导入位置，请重试验证已导入版本");
        root = installed.root;
        message = "数据校验完成，正在等待前台窗口启动。";
        Log.i("HNL-Installer", "installed verified id=" + installed.id + " bytes=" + installed.bytes + " root=" + root);
    }
    private static void marker(File root, String name, String value) throws IOException {
        try (FileOutputStream out = new FileOutputStream(new File(root, name))) { out.write(value.getBytes(StandardCharsets.UTF_8)); }
    }
    private void prepare(File data) throws IOException {
        // Data remains read-only. Runtime settings, logs and caches are app-owned.
        File runtime = new File(context.getFilesDir(), "runtime/" + NativeDataBundle.versionId(contract));
        if (!runtime.isDirectory() && !runtime.mkdirs()) throw new IOException("无法创建运行目录");
        File shaders = new File(runtime, "shaders");
        if (!shaders.isDirectory() && !shaders.mkdir()) throw new IOException("无法创建运行缓存目录");
        NativeDataBundle.stagePipelineWarmSeed(data, runtime, contract, new Stop(this));
        runtimeRoot = runtime;
        data = runtime;
        // Match the frozen runtime profile; do not import diagnostic flags from archives.
        marker(data, "gfx-target-fps", "30\n"); marker(data, "gfx-fence-spin-us", "0\n");
        marker(data, "gfx-legacy-waitall", "0\n"); marker(data, "gfx-async-pipelines", "0\n");
        marker(data, "gfx-depth-aspect", "1\n"); marker(data, "gfx-audio-paced-transport", "1\n");
        marker(data, "gfx-codex-audio-dump", "0\n"); marker(data, "gfx-codex-frame-metrics", "1\n");
        // Only an APK contract that names the archive may enable the native fallback.
        marker(data, "gfx-shader-archive", contract.required.containsKey("shaders.hnla") ? "1\n" : "0\n");
    }
    private static final class Stop implements NativeDataBundle.Cancel {
        private final DataBundleInstaller owner;
        Stop(DataBundleInstaller o) { owner = o; }
        @Override public boolean requested() { return owner.cancel; }
    }
    private static final class Updates implements NativeDataBundle.Progress {
        private final DataBundleInstaller owner;
        Updates(DataBundleInstaller o) { owner = o; }
        @Override public void update(long done, long total, String path) {
            owner.done = done;
            long now = SystemClock.elapsedRealtime();
            if (now - owner.lastProgress >= 200) {
                owner.lastProgress = now;
                owner.message = String.format(Locale.ROOT, (owner.directoryMode ? "正在检查游戏数据 %.1f%%\n%s / %s\n%s" : "正在校验导入 %.1f%%\n%s / %s\n%s"), done * 100.0 / total, size(done), size(total), path);
                owner.changed();
            }
        }
    }
    private static final class Job implements Runnable {
        private final DataBundleInstaller owner; private final int action; private final Uri uri;
        Job(DataBundleInstaller o, int a, Uri u) { owner = o; action = a; uri = u; }
        @Override public void run() {
            try {
                if (action == 0) owner.initialize();
                else if (action == 3) owner.scanDirectory();
                else if (action == 2) owner.complete(NativeDataBundle.recover(owner.stores.get(owner.selected), owner.contract,
                        new Updates(owner), new Stop(owner)));
                else {
                    try (InputStream in = owner.context.getContentResolver().openInputStream(uri)) {
                        if (in == null) throw new IOException("无法读取所选数据包，请重新选择");
                        owner.complete(NativeDataBundle.install(in, owner.stores.get(owner.selected), owner.contract,
                                new Updates(owner), new Stop(owner)));
                    }
                }
            } catch (Exception e) {
                owner.root = null;
                if (owner.directoryMode) {
                    owner.message = (e instanceof InterruptedIOException ? "检查已取消。" : "游戏数据检查失败：" + e.getMessage())
                            + "\n游戏目录和存档未改动。请检查文件是否拷贝完整，再点“重新查找并检查目录”。";
                } else {
                    owner.message = (e instanceof InterruptedIOException ? "导入已取消。" : "检查或导入失败：" + e.getMessage())
                            + "\n旧版本和存档保留。可重新选择同一数据包继续，或验证恢复已导入版本。";
                }
                Log.e("HNL-Installer", "data action=" + action + " failed", e);
            } finally { owner.busy = false; owner.changed(); }
        }
    }
}
