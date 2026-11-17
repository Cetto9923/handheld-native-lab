package org.ffrs.diagnostic;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.util.AtomicFile;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.TreeMap;

/** Version-bound startup choices. Patch data is supplied as a private APK asset. */
final class CheatMenu {
    private static final String TAG = "FFRS-Cheats";
    private static final String ASSET = "ffrs-cheats.json";
    private static final String PLAN = "ffrs-cheats.plan";
    private static final int MAX_PATCHES = 512;

    private CheatMenu() {}

    private static final class Entry {
        final String id, name, group;
        final JSONArray patches;
        final boolean r3Restore;

        Entry(JSONObject object) throws Exception {
            id = object.getString("id");
            name = object.getString("name");
            group = object.optString("group", "");
            patches = object.getJSONArray("patches");
            r3Restore = object.optBoolean("r3_restore", false);
            if (id.isEmpty() || name.isEmpty() || patches.length() == 0 || patches.length() > MAX_PATCHES) {
                throw new IOException("invalid cheat entry");
            }
        }
    }

    private static final class Catalog {
        final String buildId;
        final long textSize;
        final ArrayList<Entry> entries = new ArrayList<>();

        Catalog(JSONObject object) throws Exception {
            buildId = object.getString("build_id");
            if (!buildId.matches("[0-9a-f]{64}")) throw new IOException("invalid build id");
            textSize = hexWord(object.getString("text_size"));
            if (textSize == 0) throw new IOException("empty text segment");
            JSONArray items = object.getJSONArray("entries");
            if (items.length() > 128) throw new IOException("too many cheat entries");
            Set<String> ids = new HashSet<>();
            for (int i = 0; i < items.length(); i++) {
                Entry entry = new Entry(items.getJSONObject(i));
                if (!ids.add(entry.id)) throw new IOException("duplicate cheat id");
                entries.add(entry);
            }
        }

        boolean matches(File image) throws IOException {
            byte[] header = new byte[0x60];
            try (FileInputStream input = new FileInputStream(image)) {
                int offset = 0;
                while (offset < header.length) {
                    int count = input.read(header, offset, header.length - offset);
                    if (count < 0) throw new IOException("incomplete NSO header");
                    offset += count;
                }
            }
            if (header[0] != 'N' || header[1] != 'S' || header[2] != 'O' || header[3] != '0') {
                throw new IOException("invalid NSO header");
            }
            StringBuilder actual = new StringBuilder();
            for (int i = 0x40; i < 0x60; i++) {
                actual.append(String.format(Locale.US, "%02x", header[i] & 255));
            }
            long size = 0;
            for (int i = 0; i < 4; i++) size |= (long)(header[0x18 + i] & 255) << (8 * i);
            return buildId.equals(actual.toString()) && textSize == size;
        }
    }

    private static long hexWord(String value) throws IOException {
        if (!value.matches("[0-9a-fA-F]{1,8}")) throw new IOException("invalid patch word");
        return Long.parseLong(value, 16);
    }

    private static Catalog load(Activity activity) throws Exception {
        try (InputStream input = activity.getAssets().open(ASSET)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > 262144) throw new IOException("cheat catalog too large");
                output.write(buffer, 0, count);
            }
            return new Catalog(new JSONObject(output.toString("UTF-8")));
        }
    }

    private static SharedPreferences preferences(Activity activity) {
        return activity.getSharedPreferences("ffrs-cheats", Activity.MODE_PRIVATE);
    }

    private static void notice(Activity activity, String message) {
        Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
    }

    static void show(final Activity activity, File image, final Runnable saveEditor) {
        try {
            final Catalog catalog = load(activity);
            if (image == null || !catalog.matches(image)) {
                new AlertDialog.Builder(activity).setTitle("金手指")
                        .setMessage("玩法开关适配游戏数据1.0.1。当前数据可使用角色存档修改。")
                        .setPositiveButton("存档修改", (dialog, which) -> saveEditor.run())
                        .setNegativeButton("关闭", null).show();
                return;
            }
            final Set<String> saved = preferences(activity).getStringSet(catalog.buildId, new HashSet<String>());
            final boolean[] checked = new boolean[catalog.entries.size()];
            String[] names = new String[checked.length];
            for (int i = 0; i < checked.length; i++) {
                Entry entry = catalog.entries.get(i);
                names[i] = entry.name;
                checked[i] = saved.contains(entry.id);
            }
            new AlertDialog.Builder(activity).setTitle("金手指 · 重启游戏生效")
                    .setMultiChoiceItems(names, checked, new DialogInterface.OnMultiChoiceClickListener() {
                        @Override public void onClick(DialogInterface dialog, int which, boolean enabled) {
                            checked[which] = enabled;
                            String group = catalog.entries.get(which).group;
                            if (!enabled || group.isEmpty()) return;
                            for (int i = 0; i < checked.length; i++) {
                                if (i != which && group.equals(catalog.entries.get(i).group)) {
                                    checked[i] = false;
                                    ((AlertDialog)dialog).getListView().setItemChecked(i, false);
                                }
                            }
                        }
                    })
                    .setPositiveButton("保存", new DialogInterface.OnClickListener() {
                        @Override public void onClick(DialogInterface dialog, int which) {
                            Set<String> selected = new HashSet<>();
                            for (int i = 0; i < checked.length; i++) {
                                if (checked[i]) selected.add(catalog.entries.get(i).id);
                            }
                            save(activity, catalog.buildId, selected);
                        }
                    })
                    .setNeutralButton("角色存档修改", (dialog, which) -> saveEditor.run())
                    .setNegativeButton("取消", null)
                    .show();
            notice(activity, "勾选并保存后重启游戏生效，取消勾选并重启可恢复。");
        } catch (FileNotFoundException missing) {
            notice(activity, "此安装包尚未包含兼容的金手指。");
        } catch (Exception error) {
            Log.e(TAG, "cheat menu failed", error);
            notice(activity, "金手指读取失败，请检查游戏资源与安装包。");
        }
    }

    private static void save(Activity activity, String buildId, Set<String> selected) {
        boolean saved = preferences(activity).edit().putStringSet(buildId, selected).commit();
        if (!saved) {
            notice(activity, "金手指设置保存失败。");
            return;
        }
        new AlertDialog.Builder(activity).setTitle("金手指设置已保存")
                .setMessage("请先在游戏内保存进度。准备好后点“退出游戏”，再重新打开应用，新的勾选设置即可生效。")
                .setPositiveButton("退出游戏", (dialog, which) -> {
                    Log.i(TAG, "user requested process exit to apply startup settings");
                    activity.finishAndRemoveTask();
                    android.os.Process.killProcess(android.os.Process.myPid());
                })
                .setNegativeButton("稍后退出", null)
                .show();
    }

    private static void clearPlan(AtomicFile plan) throws IOException {
        plan.delete();
        if (plan.getBaseFile().exists()) throw new IOException("cannot clear previous startup plan");
    }

    /** Produce one atomic, fully merged startup plan before the guest thread starts. */
    static boolean prepare(Activity activity, File image, File runtimeDirectory) {
        if (runtimeDirectory == null) {
            Log.e(TAG, "runtime directory unavailable");
            notice(activity, "运行目录不可用，无法准备金手指设置。");
            return false;
        }
        AtomicFile plan = new AtomicFile(new File(runtimeDirectory, PLAN));
        FileOutputStream output = null;
        try {
            Catalog catalog;
            try {
                catalog = load(activity);
            } catch (FileNotFoundException missing) {
                clearPlan(plan);
                return true;
            }
            if (!catalog.matches(image)) {
                clearPlan(plan);
                notice(activity, "当前游戏版本与金手指不匹配，已关闭本次金手指。");
                Log.w(TAG, "startup cheats skipped because game build differs");
                return true;
            }
            Set<String> selected = preferences(activity).getStringSet(catalog.buildId, new HashSet<String>());
            TreeMap<Long, long[]> merged = new TreeMap<>();
            Set<String> groups = new HashSet<>();
            int enabled = 0;
            for (Entry entry : catalog.entries) {
                if (!selected.contains(entry.id)) continue;
                if (!entry.group.isEmpty() && !groups.add(entry.group)) {
                    throw new IOException("mutually exclusive cheat choices");
                }
                enabled++;
                for (int i = 0; i < entry.patches.length(); i++) {
                    JSONArray words = entry.patches.getJSONArray(i);
                    if (words.length() != 3) throw new IOException("invalid patch row");
                    long offset = hexWord(words.getString(0));
                    long expected = hexWord(words.getString(1));
                    long replacement = hexWord(words.getString(2));
                    long conditional = entry.r3Restore && expected != replacement ? 1 : 0;
                    if ((offset & 3) != 0) throw new IOException("unaligned patch");
                    long[] previous = merged.get(offset);
                    if (previous != null) {
                        if (previous[0] != expected) throw new IOException("conflicting patch expectations");
                        if (replacement == expected) continue;
                        if (previous[1] != expected &&
                                (previous[1] != replacement || previous[2] != conditional)) {
                            throw new IOException("conflicting patch values");
                        }
                    }
                    merged.put(offset, new long[]{expected, replacement, conditional});
                }
            }
            if (merged.size() > MAX_PATCHES) throw new IOException("too many startup patches");
            StringBuilder text = new StringBuilder("NSO-TEXT-PATCHES-1\n");
            text.append(catalog.buildId.substring(0, 32)).append('\n')
                    .append(Long.toHexString(catalog.textSize)).append('\n');
            for (java.util.Map.Entry<Long, long[]> patch : merged.entrySet()) {
                if (patch.getValue()[2] != 0) text.append("R3 ");
                text.append(String.format(Locale.US, "%08x %08x %08x\n",
                        patch.getKey(), patch.getValue()[0], patch.getValue()[1]));
            }
            output = plan.startWrite();
            output.write(text.toString().getBytes("UTF-8"));
            plan.finishWrite(output);
            output = null;
            Log.i(TAG, "startup plan prepared build=" + catalog.buildId + " enabled=" + enabled
                    + " words=" + merged.size());
            return true;
        } catch (Exception error) {
            if (output != null) plan.failWrite(output);
            Log.e(TAG, "startup cheat preparation failed", error);
            notice(activity, "金手指设置校验失败，游戏尚未启动。请在金手指菜单关闭相关选项后重试。");
            return false;
        }
    }
}
