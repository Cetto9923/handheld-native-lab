package org.ffrs.diagnostic;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.List;

import javax.crypto.Cipher;
import javax.crypto.spec.SecretKeySpec;

/**
 * The save editor must round trip: decrypt, patch, encrypt, decrypt again.
 *
 * The test builds a synthetic SVHD slot with the same property tags the game
 * writes, applies a cheat to it and then checks the file that came out: the
 * digest still covers the body, the body decrypts to the patched values and
 * the untouched bytes are kept in the backup. A slot whose digest does not
 * match its body must be refused instead of written.
 *
 * The key comes from the classpath resource the app ships
 * (assets/ffrs_save_key.bin), so the test uses the very key the editor uses.
 */
public final class SaveCheaterRoundTrip {
    private static void require(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static final int[] UNIT_IDS = {1001, 1002, 1003};
    private static final int[] UNIT_HP = {6372, 5418, 4451};
    private static final int[] UNIT_MP = {421, 489, 559};
    private static final int[] UNIT_EXP = {10000597, 10000597, 9999999};
    private static final int CAP_HP = 9999;
    private static final int CAP_MP = 9999;

    private static void write32(ByteArrayOutputStream out, int value) {
        out.write(value & 0xff);
        out.write((value >> 8) & 0xff);
        out.write((value >> 16) & 0xff);
        out.write((value >> 24) & 0xff);
    }

    /** One UE property tag: name, type, array index, size, flag byte, value. */
    private static void tag(ByteArrayOutputStream out, String name, int value) throws Exception {
        byte[] nameBytes = name.getBytes("ISO-8859-1");
        byte[] typeBytes = "IntProperty".getBytes("ISO-8859-1");
        write32(out, nameBytes.length + 1);
        out.write(nameBytes);
        out.write(0);
        write32(out, typeBytes.length + 1);
        out.write(typeBytes);
        out.write(0);
        write32(out, 0);          // array index
        write32(out, 1);          // size
        out.write(0);             // per property GUID flag
        write32(out, value);
    }

    /** The roster body the game would write for this party. */
    private static byte[] plainBody() throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (int i = 0; i < UNIT_IDS.length; i++) {
            tag(out, "UnitId", UNIT_IDS[i]);
            tag(out, "Level", 1);
            tag(out, "Experience", UNIT_EXP[i]);
            tag(out, "HitPoint", UNIT_HP[i]);
            tag(out, "MagicPoint", UNIT_MP[i]);
        }
        // The guest/party record carries the caps after the roster.
        tag(out, "HitPoint", CAP_HP);
        tag(out, "MagicPoint", CAP_MP);
        while (out.size() % 16 != 0) out.write(0);   // AES/ECB/NoPadding
        return out.toByteArray();
    }

    private static byte[] aes(byte[] key, byte[] data, boolean decrypt) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/ECB/NoPadding");
        cipher.init(decrypt ? Cipher.DECRYPT_MODE : Cipher.ENCRYPT_MODE,
                new SecretKeySpec(key, "AES"));
        return cipher.doFinal(data);
    }

    private static int le32(byte[] b, int off) {
        return (b[off] & 0xff) | ((b[off + 1] & 0xff) << 8)
             | ((b[off + 2] & 0xff) << 16) | ((b[off + 3] & 0xff) << 24);
    }

    /** The int values of the tags with the given name, in file order. */
    private static List<Integer> tagValues(byte[] buf, String name) throws Exception {
        List<Integer> out = new java.util.ArrayList<>();
        for (int off = 0; off + 8 <= buf.length; off++) {
            int nameLen = le32(buf, off);
            if (nameLen < 1 || nameLen > 80) continue;
            int nameOff = off + 4;
            if (nameOff + nameLen > buf.length) continue;
            if (!name.equals(new String(buf, nameOff, nameLen - 1, "ISO-8859-1"))) continue;
            int p = nameOff + nameLen;
            int typeLen = le32(buf, p);
            if (typeLen < 1 || typeLen > 60) continue;
            int q = p + 4 + typeLen;
            if (q + 9 > buf.length) continue;
            out.add(le32(buf, q + 9));
        }
        return out;
    }

    private static String hex(byte[] data) {
        StringBuilder sb = new StringBuilder(data.length * 2);
        for (byte b : data) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    private static byte[] readAll(File file) throws Exception {
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        }
    }

    /** Writes the SVHD container: magic, version, digest of the body, body. */
    private static void writeSlot(File file, byte[] key, byte[] plain) throws Exception {
        byte[] body = aes(key, plain, false);
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        byte[] digest = md.digest(body);
        byte[] out = new byte[32 + body.length];
        out[0] = 'S'; out[1] = 'V'; out[2] = 'H'; out[3] = 'D';
        out[4] = 0; out[5] = 0; out[6] = 1; out[7] = 0;
        System.arraycopy(digest, 0, out, 8, 20);
        System.arraycopy(body, 0, out, 32, body.length);
        try (FileOutputStream fos = new FileOutputStream(file)) { fos.write(out); }
    }

    /** The decrypted body of a slot, checked against its digest. */
    private static byte[] checkedPlain(File file, byte[] key) throws Exception {
        byte[] raw = readAll(file);
        require(raw.length > 32, "slot too short");
        require(raw[0] == 'S' && raw[1] == 'V' && raw[2] == 'H' && raw[3] == 'D', "bad magic");
        byte[] body = Arrays.copyOfRange(raw, 32, raw.length);
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        require(hex(md.digest(body)).equals(hex(Arrays.copyOfRange(raw, 8, 28))),
                "digest does not cover the body");
        return aes(key, body, true);
    }

    public static void main(String[] args) throws Exception {
        byte[] key;
        try (InputStream in = SaveCheaterRoundTrip.class.getClassLoader()
                .getResourceAsStream("assets/ffrs_save_key.bin")) {
            require(in != null, "the key asset is missing from the classpath");
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[64];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            key = out.toByteArray();
        }
        while (key.length > 0 && (key[key.length - 1] == 0 || key[key.length - 1] == '\n'
                || key[key.length - 1] == '\r' || key[key.length - 1] == ' ')) {
            key = Arrays.copyOf(key, key.length - 1);
        }
        require(key.length == 32, "the key must hold 32 bytes, got " + key.length);

        File dir = new File(System.getProperty("java.io.tmpdir"), "ffrs-save-roundtrip");
        deleteTree(dir);
        require(dir.mkdirs(), "the work directory could not be created");
        File slot = new File(dir, "GameSaveData0");

        byte[] plain = plainBody();
        // The encryption round trip itself: decrypt, encrypt, decrypt again.
        byte[] body = aes(key, plain, false);
        byte[] again = aes(key, aes(key, aes(key, body, true), false), true);
        require(Arrays.equals(plain, again),
                "decrypt, encrypt, decrypt is not the identity");
        require(Arrays.equals(body, aes(key, plain, false)),
                "the encryption is not stable");

        writeSlot(slot, key, plain);
        byte[] original = readAll(slot);

        // The editor reads the roster as it is.
        SaveCheater.Result info = SaveCheater.inspect(slot);
        require(info.ok, "the slot could not be read: " + info.message);
        require(info.units.size() == UNIT_IDS.length,
                "expected " + UNIT_IDS.length + " units, got " + info.units.size());
        for (int i = 0; i < info.units.size(); i++) {
            SaveCheater.Unit u = info.units.get(i);
            require(u.unitId == UNIT_IDS[i], "unit order changed");
            require(u.hitPoint == UNIT_HP[i], "hp of unit " + u.unitId);
            require(u.magicPoint == UNIT_MP[i], "mp of unit " + u.unitId);
            require(u.experience == UNIT_EXP[i], "exp of unit " + u.unitId);
            require(u.maxHitPoint == CAP_HP, "hp cap");
            require(u.maxMagicPoint == CAP_MP, "mp cap");
        }

        // Apply the cheat.
        SaveCheater.Result applied = SaveCheater.apply(slot, 8000, 9000, 700, 800, 424242, 7, null);
        require(applied.ok, "the cheat was refused: " + applied.message);
        require(!applied.edits.isEmpty(), "the cheat changed nothing");
        require(applied.backupCreated, "the untouched slot was not kept aside");

        // The file that came out is consistent and holds the new values.
        byte[] patched = checkedPlain(slot, key);
        require(!Arrays.equals(patched, plain), "the body did not change");
        SaveCheater.Result after = SaveCheater.inspect(slot);
        require(after.ok, "the patched slot could not be read: " + after.message);
        for (SaveCheater.Unit u : after.units) {
            require(u.hitPoint == 8000, "hp of unit " + u.unitId + " is " + u.hitPoint);
            require(u.maxHitPoint == 9000, "hp cap is " + u.maxHitPoint);
            require(u.magicPoint == 700, "mp of unit " + u.unitId + " is " + u.magicPoint);
            require(u.maxMagicPoint == 800, "mp cap is " + u.maxMagicPoint);
            require(u.experience == 424242, "exp of unit " + u.unitId + " is " + u.experience);
        }
        // The level is not part of the roster the editor reports, so it is
        // checked in the body itself: every unit carries the new level.
        List<Integer> levels = tagValues(patched, "Level");
        require(levels.size() == UNIT_IDS.length,
                "expected " + UNIT_IDS.length + " level tags, got " + levels.size());
        for (Integer level : levels) require(level == 7, "a level tag is " + level);
        // The roster values and the party caps are both written.
        List<Integer> hitPoints = tagValues(patched, "HitPoint");
        require(hitPoints.size() == UNIT_IDS.length + 1,
                "expected " + (UNIT_IDS.length + 1) + " HitPoint tags, got " + hitPoints.size());
        for (int i = 0; i < UNIT_IDS.length; i++) {
            require(hitPoints.get(i) == 8000, "HitPoint tag " + i + " is " + hitPoints.get(i));
        }
        require(hitPoints.get(UNIT_IDS.length) == 9000,
                "the party hp cap is " + hitPoints.get(UNIT_IDS.length));
        // The size did not change: the container is fixed width.
        require(readAll(slot).length == original.length, "the slot size changed");

        // The untouched copy is still the untouched copy.
        File backup = SaveCheater.backupFile(slot);
        require(backup.isFile(), "the backup is missing");
        require(Arrays.equals(readAll(backup), original), "the backup is not the original slot");

        // A slot whose digest does not match its body is refused, not written.
        File broken = new File(dir, "GameSaveData1");
        byte[] raw = original.clone();
        raw[40] ^= 0x40;
        try (FileOutputStream fos = new FileOutputStream(broken)) { fos.write(raw); }
        byte[] before = readAll(broken);
        SaveCheater.Result refused = SaveCheater.apply(broken, 1, 2, 3, 4, 5, 6, null);
        require(!refused.ok, "a slot with a broken digest was accepted");
        require(refused.message.contains("校验"), "the refusal does not say why: " + refused.message);
        require(Arrays.equals(readAll(broken), before), "a refused slot was still written");

        // A file that is not a slot at all is refused as well.
        File alien = new File(dir, "GameSaveData2");
        try (FileOutputStream fos = new FileOutputStream(alien)) { fos.write(new byte[64]); }
        SaveCheater.Result notASlot = SaveCheater.apply(alien, 1, 2, 3, 4, 5, 6, null);
        require(!notASlot.ok, "a file that is not a slot was accepted");

        // The message has to say that the running game is not changed.
        require(applied.message.contains("本局不生效"),
                "the message does not say that the session is unchanged: " + applied.message);

        deleteTree(dir);
        System.out.println("save round trip checks passed; the running session is not touched");
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }
}
