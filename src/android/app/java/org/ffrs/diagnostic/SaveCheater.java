package org.ffrs.diagnostic;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;

/**
 * In-app save editor for the FFRS "SVHD" save slots.
 *
 * Layout of a slot: magic "SVHD", int32 version, 20 byte SHA-1 of the body,
 * 4 unused bytes, then an AES-256-ECB encrypted body. The body is a stream of
 * UE4 property tags; the unit roster repeats UnitId / Experience / HitPoint /
 * MagicPoint per unit, and a guest/party record after the roster carries the
 * same two names again as the caps.
 *
 * The 32 byte key ships in assets/ffrs_save_key.bin.
 */
public final class SaveCheater {
    private static final String TAG = "FFRS-SaveCheat";

    private static final byte[] MAGIC = {'S', 'V', 'H', 'D'};
    private static final int HEADER_LEN = 32;
    private static final int DEFAULT_VERSION = 0x00010000;

    /** Int properties that make up one roster unit record. */
    private static final String[] UNIT_TAGS = {"UnitId", "Experience", "HitPoint", "MagicPoint"};

    private SaveCheater() {}

    /** One int32 property found in the decrypted body. */
    public static final class IntTag {
        public final String name;
        public final int value;
        public final int valueOffset;   // offset of the int32 inside the plain body
        IntTag(String name, int value, int valueOffset) {
            this.name = name; this.value = value; this.valueOffset = valueOffset;
        }
    }

    /** One roster unit with the offsets of its four int properties. */
    public static final class Unit {
        public int unitId;
        public int experience;
        public int hitPoint;
        public int magicPoint;
        public int hitPointOffset = -1;   // current HP
        public int magicPointOffset = -1;  // current MP
        public int experienceOffset = -1;
        public int unitIdOffset = -1;
        // caps (guest/party record), -1 when not found
        public int maxHitPoint = -1;
        public int maxMagicPoint = -1;
        public int maxHitPointOffset = -1;
        public int maxMagicPointOffset = -1;
        public int level = 1;
        public int levelOffset = -1;
    }

    /** Outcome of a cheat run, suitable for showing to the user. */
    public static final class Result {
        public boolean ok;
        public String message = "";
        /** True when this run wrote the untouched slot aside first. */
        public boolean backupCreated;
        public final List<String> edits = new ArrayList<>();
        public final List<Unit> units = new ArrayList<>();
        public String savePath;
    }

    // ---------------------------------------------------------------- key

    /** Logs a warning without letting a missing Android runtime break the caller. */
    private static void warn(String message) {
        try { Log.w(TAG, message); } catch (Throwable ignored) { }
    }

    private static byte[] cachedKey;

    /**
     * Load the 32 byte AES key.
     *
     * On the device the key ships as an asset. The file fallback keeps the
     * class usable outside the app (host side tooling and tests).
     */
    public static synchronized byte[] key() {
        if (cachedKey != null) return cachedKey;
        InputStream in = null;
        try {
            in = SaveCheater.class.getClassLoader().getResourceAsStream("assets/ffrs_save_key.bin");
            if (in == null) return keyFromFile();
            byte[] raw = readAll(in);
            byte[] trimmed = trim(raw);
            if (trimmed.length != 32) {
                warn("save key must be 32 bytes, got " + trimmed.length);
                return null;
            }
            cachedKey = trimmed;
            return cachedKey;
        } catch (Exception e) {
            warn("save key load failed: " + e);
            return null;
        } finally {
            close(in);
        }
    }

    /** Fallback: read the key from the well known local path. */
    private static byte[] keyFromFile() {
        String[] paths = {
                ".local/cheat/save_key.txt",
                "src/android/app/gamewindow/assets/ffrs_save_key.bin"
        };
        for (String p : paths) {
            File f = new File(p);
            if (!f.isFile()) continue;
            try {
                InputStream in = new FileInputStream(f);
                byte[] trimmed;
                try { trimmed = trim(readAll(in)); } finally { close(in); }
                if (trimmed.length != 32) {
                    warn("save key at " + p + " holds " + trimmed.length + " bytes, expected 32");
                    continue;
                }
                cachedKey = trimmed;
                return cachedKey;
            } catch (Exception e) {
                warn("save key read failed at " + p + ": " + e);
            }
        }
        return null;
    }

    /** True when the key is present, i.e. the cheat menu can run. */
    public static boolean available() { return key() != null; }

    // ------------------------------------------------------------- crypto

    private static byte[] aes(byte[] key, byte[] data, boolean decrypt) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/ECB/NoPadding");
        cipher.init(decrypt ? Cipher.DECRYPT_MODE : Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"));
        return cipher.doFinal(data);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }

    private static byte[] trim(byte[] raw) {
        int end = raw.length;
        while (end > 0) {
            int b = raw[end - 1] & 0xff;
            if (b == 0 || b == ' ' || b == '\n' || b == '\r' || b == '\t') end--;
            else break;
        }
        byte[] out = new byte[end];
        System.arraycopy(raw, 0, out, 0, end);
        return out;
    }

    private static void close(java.io.Closeable c) {
        if (c == null) return;
        try { c.close(); } catch (Exception ignored) {}
    }

    // ------------------------------------------------------------ parsing

    /**
     * Decode one property tag header at {@code off}.
     *
     * Layout: int32 name_len (includes the NUL), ANSI name, int32 type_len
     * (includes the NUL), ANSI type, int32 arr_idx, int32 size, then the
     * value. IntProperty carries one extra byte (a per-property GUID flag)
     * between the size and the int32 value.
     *
     * @return the parsed header, or null when the bytes are not a tag.
     */
    private static long[] readTag(byte[] buf, int off) {
        // name_len, name_off, type_len, type_off, array_index, size, value_off
        if (off + 8 > buf.length) return null;
        int nameLen = le32(buf, off);
        if (nameLen < 1 || nameLen > 80) return null;
        int nameOff = off + 4;
        if (nameOff + nameLen > buf.length) return null;
        int p = nameOff + nameLen;
        if (p + 4 > buf.length) return null;
        int typeLen = le32(buf, p);
        if (typeLen < 1 || typeLen > 60) return null;
        int typeOff = p + 4;
        int q = typeOff + typeLen;
        if (q + 8 > buf.length) return null;
        return new long[] {nameOff, typeOff, q + 8};
    }

    private static int le32(byte[] b, int off) {
        return (b[off] & 0xff) | ((b[off + 1] & 0xff) << 8)
             | ((b[off + 2] & 0xff) << 16) | ((b[off + 3] & 0xff) << 24);
    }

    private static String ascii(byte[] b, int off, int len) {
        StringBuilder sb = new StringBuilder(len);
        for (int i = 0; i < len; i++) {
            int c = b[off + i] & 0xff;
            if (c == 0) break;
            sb.append((char) c);
        }
        return sb.toString();
    }

    /** Every IntProperty tag with its int32 value and value offset. */
    private static List<IntTag> findIntTags(byte[] buf, java.util.Set<String> wanted) {
        List<IntTag> out = new ArrayList<>();
        int limit = buf.length - 16;
        for (int off = 0; off < limit; off++) {
            long[] tag = readTag(buf, off);
            if (tag == null) continue;
            String name = ascii(buf, (int) tag[0], 80);
            if (!wanted.contains(name)) continue;
            String type = ascii(buf, (int) tag[1], 60);
            if (!"IntProperty".equals(type)) continue;
            int valueOff = (int) tag[2] + 1;      // skip the GUID flag byte
            if (valueOff + 4 > buf.length) continue;
            out.add(new IntTag(name, le32(buf, valueOff), valueOff));
        }
        return out;
    }

    /**
     * Group the roster into per-unit records.
     *
     * The roster is written unit by unit (UnitId, Experience, HitPoint,
     * MagicPoint, ...), so the tags appear in that order.
     */
    private static List<Unit> groupUnits(byte[] buf) {
        java.util.Set<String> wanted = new java.util.HashSet<>();
        for (String t : UNIT_TAGS) wanted.add(t);
        List<IntTag> tags = findIntTags(buf, wanted);

        List<Unit> units = new ArrayList<>();
        Unit current = null;
        for (IntTag t : tags) {
            if ("UnitId".equals(t.name)) {
                if (current != null) units.add(current);
                current = new Unit();
                current.unitId = t.value;
                current.unitIdOffset = t.valueOffset;
            } else if (current != null) {
                if ("Experience".equals(t.name)) {
                    if (current.experienceOffset < 0) { current.experience = t.value; current.experienceOffset = t.valueOffset; }
                } else if ("HitPoint".equals(t.name)) {
                    if (current.hitPointOffset < 0) { current.hitPoint = t.value; current.hitPointOffset = t.valueOffset; }
                } else if ("MagicPoint".equals(t.name)) {
                    if (current.magicPointOffset < 0) { current.magicPoint = t.value; current.magicPointOffset = t.valueOffset; }
                }
            }
        }
        if (current != null) units.add(current);
        return units;
    }

    /**
     * The guest/party caps.
     *
     * The roster unit current values come first; the extra HitPoint /
     * MagicPoint pair after the whole roster belongs to the guest record
     * (unitGuest -> Party -> partyList -> SaveData_UnitGuest).
     */
    private static void readCaps(byte[] buf, List<Unit> units) {
        java.util.Set<String> wanted = new java.util.HashSet<>();
        wanted.add("HitPoint");
        wanted.add("MagicPoint");
        List<IntTag> caps = findIntTags(buf, wanted);

        // Offsets of the values already claimed by the roster.
        Map<Integer, Boolean> rosterValues = new LinkedHashMap<>();
        int rosterEnd = -1;
        for (Unit u : units) {
            if (u.hitPointOffset >= 0) { rosterValues.put(u.hitPointOffset, true); rosterEnd = Math.max(rosterEnd, u.hitPointOffset); }
            if (u.magicPointOffset >= 0) { rosterValues.put(u.magicPointOffset, true); rosterEnd = Math.max(rosterEnd, u.magicPointOffset); }
        }

        List<IntTag> extraHp = new ArrayList<>();
        List<IntTag> extraMp = new ArrayList<>();
        for (IntTag t : caps) {
            if (rosterValues.containsKey(t.valueOffset)) continue;
            if ("HitPoint".equals(t.name)) extraHp.add(t); else extraMp.add(t);
        }

        // The guest/party cap block is the one right after the roster.
        IntTag capHp = null, capMp = null;
        if (extraHp.size() == 1) capHp = extraHp.get(0);
        if (extraMp.size() == 1) capMp = extraMp.get(0);
        if (capHp != null && capMp != null) {
            for (Unit u : units) {
                u.maxHitPoint = capHp.value; u.maxHitPointOffset = capHp.valueOffset;
                u.maxMagicPoint = capMp.value; u.maxMagicPointOffset = capMp.valueOffset;
            }
        }
    }

    // -------------------------------------------------------------- apply

    /**
     * Apply the requested edits to a save slot in place.
     *
     * @param file      the slot to edit (GameSaveData0)
     * @param hp        current HP, or null to leave it alone
     * @param maxHp     HP cap, or null
     * @param mp        current MP, or null
     * @param maxMp     MP cap, or null
     * @param exp       experience, or null
     * @param level     level, or null
     */
    public static Result apply(File file, Integer hp, Integer maxHp, Integer mp, Integer maxMp,
                               Integer exp, Integer level) {
        return apply(file, hp, maxHp, mp, maxMp, exp, level, null);
    }

    /**
     * Apply the requested edits to a save slot in place.
     *
     * @param file      the slot to edit (GameSaveData0)
     * @param hp        current HP, or null to leave it alone
     * @param maxHp     HP cap, or null
     * @param mp        current MP, or null
     * @param maxMp     MP cap, or null
     * @param exp       experience, or null
     * @param level     level, or null
     * @param onlyUnit  unit id to restrict the edit to, or null for every unit
     */
    public static Result apply(File file, Integer hp, Integer maxHp, Integer mp, Integer maxMp,
                               Integer exp, Integer level, Integer onlyUnit) {
        Result result = new Result();
        result.savePath = file == null ? null : file.getAbsolutePath();
        byte[] key = key();
        if (key == null) {
            result.message = "存档密钥缺失，无法编辑";
            return result;
        }
        if (file == null || !file.exists()) {
            result.message = "存档文件不存在";
            return result;
        }

        byte[] raw;
        InputStream in = null;
        try {
            in = new FileInputStream(file);
            raw = readAll(in);
        } catch (IOException e) {
            result.message = "读取存档失败: " + e.getMessage();
            return result;
        } finally {
            close(in);
        }

        if (raw.length < HEADER_LEN || raw[0] != MAGIC[0] || raw[1] != MAGIC[1]
                || raw[2] != MAGIC[2] || raw[3] != MAGIC[3]) {
            result.message = "不是有效的 SVHD 存档";
            return result;
        }

        int version = le32(raw, 4);
        byte[] digest = new byte[20];
        System.arraycopy(raw, 8, digest, 0, 20);
        byte[] body = new byte[raw.length - HEADER_LEN];
        System.arraycopy(raw, HEADER_LEN, body, 0, body.length);

        byte[] plain;
        try {
            plain = aes(key, body, true);
        } catch (Exception e) {
            result.message = "解密失败: " + e.getMessage();
            return result;
        }

        // The stored SHA-1 covers the body; refuse to touch a file that does not match.
        if (!sha1(body).equals(bytesToHex(digest))) {
            result.message = "存档校验不通过（SHA-1 不匹配），拒绝修改";
            return result;
        }

        List<Unit> units = groupUnits(plain);
        readCaps(plain, units);
        result.units.addAll(units);
        if (units.isEmpty()) {
            result.message = "没有找到单位数据";
            return result;
        }

        for (Unit u : units) {
            if (onlyUnit != null && u.unitId != onlyUnit) continue;
            if (hp != null) put(plain, u.hitPointOffset, hp, result, u.unitId, "HP");
            if (maxHp != null) put(plain, u.maxHitPointOffset, maxHp, result, u.unitId, "HP上限");
            if (mp != null) put(plain, u.magicPointOffset, mp, result, u.unitId, "MP");
            if (maxMp != null) put(plain, u.maxMagicPointOffset, maxMp, result, u.unitId, "MP上限");
            if (exp != null) put(plain, u.experienceOffset, exp, result, u.unitId, "经验");
            if (level != null) putLevel(plain, u, level, result);
        }

        byte[] outBody;
        try {
            outBody = aes(key, plain, false);
        } catch (Exception e) {
            result.message = "加密失败: " + e.getMessage();
            return result;
        }
        // Header layout: magic(4) + version(4) + sha1(20) + 4 unused bytes + body.
        byte[] out = new byte[HEADER_LEN + outBody.length];
        System.arraycopy(MAGIC, 0, out, 0, 4);
        out[4] = (byte) (version & 0xff);
        out[5] = (byte) ((version >> 8) & 0xff);
        out[6] = (byte) ((version >> 16) & 0xff);
        out[7] = (byte) ((version >> 24) & 0xff);
        byte[] newDigest = hexToBytes(sha1(outBody));
        for (int i = 0; i < 20; i++) out[8 + i] = newDigest[i];
        // out[28..32] stays zero.
        System.arraycopy(outBody, 0, out, HEADER_LEN, outBody.length);

        if (out.length != raw.length) {
            result.message = "修改后大小变化，已放弃写入";
            return result;
        }

        // Keep the untouched slot aside before the first edit, so the
        // function menu can roll the change back.
        ensureBackup(file, result);

        if (!install(file, out, result)) return result;

        // Round trip check on the produced file.
        try {
            byte[] check = readAll(new FileInputStream(file));
            byte[] checkBody = new byte[check.length - HEADER_LEN];
            System.arraycopy(check, HEADER_LEN, checkBody, 0, checkBody.length);
            byte[] checkPlain = aes(key, checkBody, true);
            if (!java.util.Arrays.equals(checkPlain, plain)) {
                result.message = "回读校验失败";
                return result;
            }
        } catch (Exception e) {
            result.message = "回读校验失败: " + e.getMessage();
            return result;
        }

        result.ok = true;
        if (result.edits.isEmpty()) result.message = "没有需要修改的数值";
        else result.message = "已修改 " + result.edits.size() + " 项"
                + (result.backupCreated ? "（已备份原存档，可用菜单还原）" : "")
                // The running game keeps its own copy of the roster in the
                // guest memory, and that runtime layout is not mapped yet, so
                // the edit cannot be pushed into the running session safely.
                // Say so plainly instead of pretending the session changed.
                + "\n本局不生效：运行中的数值在 guest 内存里，存档只改到下次读档"
                + "\n回标题画面 Load Game 重新读档后生效";
        return result;
    }

    /** The copy of the slot that was taken before the first cheat edit. */
    public static File backupFile(File file) {
        return new File(file.getParentFile(), file.getName() + ".orig");
    }

    /**
     * Copies the slot aside before the first edit.
     *
     * The backup is written once and then kept: later edits still roll back to
     * the original save, not to the previously cheated one.
     */
    private static void ensureBackup(File file, Result result) {
        File backup = backupFile(file);
        if (backup.exists()) return;
        InputStream in = null;
        OutputStream out = null;
        try {
            in = new FileInputStream(file);
            out = new FileOutputStream(backup);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.flush();
            result.backupCreated = true;
            warn("save backup written: " + backup.getAbsolutePath());
        } catch (IOException e) {
            warn("save backup failed: " + e.getMessage());
            close(in); close(out);
            backup.delete();
        } finally {
            close(in); close(out);
        }
    }

    /** True when the untouched copy of the slot is on disk. */
    public static boolean hasOriginal(File file) {
        return file != null && backupFile(file).isFile();
    }

    /**
     * The roster as it was before any cheat edit.
     *
     * Reads the untouched copy that the first cheat edit left behind. When
     * that copy is missing the current slot is the best guess there is, and
     * the message says so instead of pretending the numbers are original.
     */
    public static Result original(File file) {
        if (file == null) {
            Result empty = new Result();
            empty.message = "存档文件不存在";
            return empty;
        }
        File backup = backupFile(file);
        if (backup.isFile()) {
            Result result = inspect(backup);
            result.message = "来自原存档备份 " + backup.getName();
            return result;
        }
        Result result = inspect(file);
        result.message = "没有原存档备份，当前值即原始值";
        return result;
    }

    /**
     * Puts the backup copy back over the slot.
     *
     * The guest keeps its own copy of the roster, so the game has to be
     * restarted before the restored values are visible in game.
     */
    public static Result restore(File file) {
        Result result = new Result();
        result.savePath = file == null ? null : file.getAbsolutePath();
        if (file == null || !file.exists()) {
            result.message = "存档文件不存在";
            return result;
        }
        File backup = backupFile(file);
        if (!backup.exists()) {
            result.message = "没有还原备份（首次修改前会自动备份原存档）";
            return result;
        }
        byte[] raw;
        InputStream in = null;
        try {
            in = new FileInputStream(backup);
            raw = readAll(in);
        } catch (IOException e) {
            result.message = "读取备份失败: " + e.getMessage();
            return result;
        } finally {
            close(in);
        }
        if (raw.length < HEADER_LEN || raw[0] != MAGIC[0] || raw[1] != MAGIC[1]
                || raw[2] != MAGIC[2] || raw[3] != MAGIC[3]) {
            result.message = "备份文件不是有效的 SVHD 存档";
            return result;
        }
        byte[] body = new byte[raw.length - HEADER_LEN];
        System.arraycopy(raw, HEADER_LEN, body, 0, body.length);
        byte[] digest = new byte[20];
        System.arraycopy(raw, 8, digest, 0, 20);
        if (!sha1(body).equals(bytesToHex(digest))) {
            result.message = "备份文件校验不通过（SHA-1 不匹配）";
            return result;
        }
        if (!install(file, raw, result)) return result;
        result.ok = true;
        result.message = "已还原存档（重启游戏后生效）";
        result.edits.add("已从 " + backup.getName() + " 还原");
        return result;
    }

    /**
     * Writes the slot back.
     *
     * The slot file may belong to another uid (an adb pushed file keeps the
     * shell owner), and then opening it for write fails with EACCES. Writing
     * a fresh file in the same directory and renaming it over the slot only
     * needs write permission on the directory, which the app owns.
     */
    private static boolean install(File file, byte[] data, Result result) {
        File temp = new File(file.getParentFile(), file.getName() + ".cheat.tmp");
        OutputStream os = null;
        try {
            os = new FileOutputStream(temp);
            os.write(data);
            os.flush();
        } catch (IOException e) {
            close(os);
            result.message = "写入存档失败: " + e.getMessage();
            return false;
        } finally {
            close(os);
        }
        try {
            // rename() inside the same directory only needs write permission on
            // the directory, not on the replaced file.
            if (!temp.renameTo(file)) throw new IOException("rename failed");
        } catch (Exception e) {
            // Fall back to a plain copy when the rename is not permitted.
            try {
                java.io.InputStream src = new FileInputStream(temp);
                OutputStream dst = new FileOutputStream(file);
                try {
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = src.read(buf)) > 0) dst.write(buf, 0, n);
                    dst.flush();
                } finally { close(src); close(dst); }
            } catch (IOException e2) {
                result.message = "写入存档失败: " + e2.getMessage();
                return false;
            }
        }
        return true;
    }

    private static void put(byte[] plain, int offset, int value, Result result, int unitId, String label) {
        // The offset comes from the parsed tag, so it must point at a whole
        // int32 inside the decrypted body before anything is written.
        if (offset < 0 || offset + 4 > plain.length) {
            result.edits.add("单位" + unitId + " " + label + " 偏移非法（已跳过）");
            return;
        }
        int old = le32(plain, offset);
        if (old == value) return;
        plain[offset] = (byte) (value & 0xff);
        plain[offset + 1] = (byte) ((value >> 8) & 0xff);
        plain[offset + 2] = (byte) ((value >> 16) & 0xff);
        plain[offset + 3] = (byte) ((value >> 24) & 0xff);
        result.edits.add("单位" + unitId + " " + label + " " + old + " -> " + value);
    }

    /**
     * Level lives next to the unit's other int properties; the tag name differs
     * between builds, so look it up by scanning the unit record.
     */
    private static void putLevel(byte[] plain, Unit u, int level, Result result) {
        if (u.levelOffset >= 0) {
            put(plain, u.levelOffset, level, result, u.unitId, "等级");
            return;
        }
        java.util.Set<String> wanted = new java.util.HashSet<>();
        wanted.add("Level");
        wanted.add("UnitLevel");
        wanted.add("LevelValue");
        List<IntTag> tags = findIntTags(plain, wanted);
        for (IntTag t : tags) {
            if (t.valueOffset > u.unitIdOffset && t.valueOffset < u.hitPointOffset + 4096) {
                u.levelOffset = t.valueOffset;
                u.level = level;
                put(plain, t.valueOffset, level, result, u.unitId, "等级");
                return;
            }
        }
        result.edits.add("单位" + u.unitId + " 等级 未找到可写字段（已跳过）");
    }

    /**
     * The decrypted save body, or null when the slot is missing or its digest
     * does not match. The guest memory scan uses it to look for the live copy.
     */
    public static byte[] plaintext(File file) {
        byte[] key = key();
        if (key == null || file == null || !file.exists()) return null;
        byte[] raw;
        InputStream in = null;
        try {
            in = new FileInputStream(file);
            raw = readAll(in);
        } catch (IOException e) {
            return null;
        } finally {
            close(in);
        }
        if (raw.length < HEADER_LEN || raw[0] != MAGIC[0] || raw[1] != MAGIC[1]
                || raw[2] != MAGIC[2] || raw[3] != MAGIC[3]) return null;
        byte[] digest = new byte[20];
        System.arraycopy(raw, 8, digest, 0, 20);
        byte[] body = new byte[raw.length - HEADER_LEN];
        System.arraycopy(raw, HEADER_LEN, body, 0, body.length);
        if (!sha1(body).equals(bytesToHex(digest))) return null;
        try {
            return aes(key, body, true);
        } catch (Exception e) {
            return null;
        }
    }

    /** Read the current unit roster without changing anything. */
    public static Result inspect(File file) {
        Result result = new Result();
        result.savePath = file == null ? null : file.getAbsolutePath();
        byte[] key = key();
        if (key == null) { result.message = "存档密钥缺失"; return result; }
        if (file == null || !file.exists()) { result.message = "存档文件不存在"; return result; }
        try {
            byte[] raw = readAll(new FileInputStream(file));
            if (raw.length < HEADER_LEN) { result.message = "存档文件过短"; return result; }
            byte[] body = new byte[raw.length - HEADER_LEN];
            System.arraycopy(raw, HEADER_LEN, body, 0, body.length);
            byte[] plain = aes(key, body, true);
            List<Unit> units = groupUnits(plain);
            readCaps(plain, units);
            result.units.addAll(units);
            result.ok = true;
            result.message = "读取到 " + units.size() + " 个单位";
        } catch (Exception e) {
            result.message = "读取失败: " + e.getMessage();
        }
        return result;
    }

    /** SHA-1 of the given bytes, as a lower case hex string. */
    private static String sha1(byte[] data) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            return bytesToHex(md.digest(data));
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    /** Parses a lower case hex string back into bytes. */
    private static byte[] hexToBytes(String hex) {
        int n = hex.length() / 2;
        byte[] out = new byte[n];
        for (int i = 0; i < n; i++) {
            out[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
        }
        return out;
    }

    private static String bytesToHex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (byte x : b) sb.append(String.format("%02x", x));
        return sb.toString();
    }
}
