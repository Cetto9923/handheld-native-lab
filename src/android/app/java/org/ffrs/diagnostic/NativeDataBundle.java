package org.ffrs.diagnostic;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Streaming, versioned local-data import. No Android, game execution or save writes. */
public final class NativeDataBundle {
    public interface Progress { void update(long done, long total, String path); }
    public interface Cancel { boolean requested(); }
    public static final class Contract {
        public final String nativeSha, indexSha;
        public final Map<String, String> required;
        public Contract(String nativeSha, String indexSha, Map<String, String> required) {
            if (!hex(nativeSha) || !hex(indexSha)) throw new IllegalArgumentException("Invalid trusted contract");
            this.nativeSha = nativeSha;
            this.indexSha = indexSha;
            this.required = new HashMap<String, String>(required);
            for (Map.Entry<String, String> e : this.required.entrySet()) {
                validPath(e.getKey());
                if (!hex(e.getValue())) throw new IllegalArgumentException("Invalid required hash");
            }
        }
    }
    public static final class Installed {
        public final File root;
        public final String id;
        public final long bytes;
        Installed(File root, String id, long bytes) { this.root = root; this.id = id; this.bytes = bytes; }
    }
    private static final long MAX_PAYLOAD = 64L * 1024 * 1024 * 1024;
    private static final int MAX_FILES = 200000;
    private static final int MAX_INDEX = 32 * 1024 * 1024;
    private static final String HEADER = "bundle.properties", INDEX = "files.tsv";
    private static final Set<String> FIELDS = new HashSet<String>(Arrays.asList(
        "format", "abi", "bundle_id", "native_sha256", "index_sha256", "file_count", "payload_bytes"));

    private NativeDataBundle() {}
    private static boolean hex(String s) { return s != null && s.matches("[0-9a-f]{64}"); }
    private static MessageDigest digest() throws IOException {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (Exception e) { throw new IOException("SHA-256 unavailable", e); }
    }
    private static String hexBytes(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte v : b) s.append(String.format(Locale.ROOT, "%02x", v & 255));
        return s.toString();
    }
    private static void cancelled(Cancel c) throws IOException {
        if (c != null && c.requested()) throw new InterruptedIOException("Data import cancelled; previous installation preserved");
    }
    private static void validPath(String s) {
        if (s == null || s.length() == 0 || s.length() > 1024 || s.startsWith("/")
                || s.indexOf('\\') >= 0 || s.indexOf(':') >= 0 || s.indexOf('\0') >= 0
                || s.indexOf('\t') >= 0 || s.indexOf('\n') >= 0 || s.indexOf('\r') >= 0)
            throw new IllegalArgumentException("Invalid archive path");
        for (String part : s.split("/", -1))
            if (part.length() == 0 || part.equals(".") || part.equals(".."))
                throw new IllegalArgumentException("Invalid archive path segment");
        if (s.startsWith(".hnl-") || s.equals("ready.properties"))
            throw new IllegalArgumentException("Reserved archive path");
    }
    private static ZipEntry next(ZipInputStream z, String expected) throws IOException {
        ZipEntry e = z.getNextEntry();
        if (e == null || e.isDirectory() || !expected.equals(e.getName()))
            throw new IOException("Unexpected or missing data entry: " + expected);
        return e;
    }
    private static Map<String, String> properties(byte[] data) throws IOException {
        Map<String, String> p = new HashMap<String, String>();
        String s = new String(data, StandardCharsets.UTF_8);
        for (String line : s.split("\n")) {
            int at = line.indexOf('=');
            if (at <= 0 || line.indexOf('\r') >= 0) throw new IOException("Invalid bundle header");
            String key = line.substring(0, at), value = line.substring(at + 1);
            if (p.put(key, value) != null) throw new IOException("Duplicate bundle header key");
        }
        if (!p.keySet().equals(FIELDS)) throw new IOException("Unsupported bundle header fields");
        return p;
    }
    private static byte[] bounded(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] b = new byte[8192]; int n;
        while ((n = in.read(b)) != -1) {
            if (out.size() > limit - n) throw new IOException("Metadata size limit exceeded");
            out.write(b, 0, n);
        }
        return out.toByteArray();
    }
    private static long number(String s, long max) throws IOException {
        if (s == null || !s.matches("[0-9]+")) throw new IOException("Invalid bundle length");
        try {
            long v = Long.parseLong(s);
            if (v < 1 || v > max) throw new IOException("Bundle length outside supported bounds");
            return v;
        } catch (NumberFormatException e) { throw new IOException("Invalid bundle length", e); }
    }
    private static final class Row {
        final String path, sha; final long size;
        Row(String line) throws IOException {
            if (line == null || line.length() > 1200) throw new IOException("Invalid data index row");
            String[] s = line.split("\t", -1);
            if (s.length != 3 || !hex(s[0])) throw new IOException("Invalid data index digest");
            try { validPath(s[2]); } catch (IllegalArgumentException e) { throw new IOException(e); }
            sha = s[0]; size = number(s[1], MAX_PAYLOAD); path = s[2];
        }
    }
    private static BufferedReader reader(File f) throws IOException {
        return new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8));
    }
    private static void syncWrite(File f, byte[] b) throws IOException {
        try (FileOutputStream out = new FileOutputStream(f)) { out.write(b); out.getFD().sync(); }
    }

    /** Contract comes from the installed APK, not the untrusted archive header. */
    public static Installed install(InputStream source, File store, Contract contract,
                                    Progress progress, Cancel cancel) throws IOException {
        if (!store.isDirectory() && !store.mkdirs()) throw new IOException("Cannot create installation directory");
        if (Files.isSymbolicLink(store.toPath())) throw new IOException("Installation directory must not be a link");
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source, 128 * 1024))) {
            next(zip, HEADER);
            Map<String, String> meta = properties(bounded(zip, 4096));
            if (!"hnl-native-data/1".equals(meta.get("format")) || !"arm64-v8a".equals(meta.get("abi")))
                throw new IOException("Unsupported data format or ABI");
            String id = hexBytes(digest().digest((contract.indexSha + "\n" + contract.nativeSha)
                    .getBytes(StandardCharsets.UTF_8)));
            if (!contract.nativeSha.equals(meta.get("native_sha256"))
                    || !contract.indexSha.equals(meta.get("index_sha256"))
                    || !id.equals(meta.get("bundle_id")))
                throw new IOException("Application and data package versions do not match");
            long count = number(meta.get("file_count"), MAX_FILES);
            long total = number(meta.get("payload_bytes"), MAX_PAYLOAD);
            long reserve = Math.max(256L * 1024 * 1024, total / 20);
            if (store.getUsableSpace() < total + reserve) throw new IOException("Not enough storage for verified import");
            cancelled(cancel);
            File destination = new File(store, id);
            // Never silently merge into a prior complete or incomplete version.
            if (destination.exists()) throw new IOException("This data version already exists; keep it or use recovery");
            File stage = new File(store, ".hnl-import-" + UUID.randomUUID().toString());
            if (!stage.mkdir()) throw new IOException("Cannot create import staging directory");
            File index = new File(stage, ".hnl-index.tsv");
            next(zip, INDEX);
            MessageDigest indexDigest = digest();
            try (FileOutputStream out = new FileOutputStream(index)) {
                byte[] b = new byte[65536]; int n, length = 0;
                while ((n = zip.read(b)) != -1) {
                    cancelled(cancel);
                    if (length > MAX_INDEX - n) throw new IOException("Index size limit exceeded");
                    length += n; out.write(b, 0, n); indexDigest.update(b, 0, n);
                }
                out.getFD().sync();
            }
            if (!contract.indexSha.equals(hexBytes(indexDigest.digest()))) throw new IOException("Data index checksum mismatch");
            long entries = 0, bytes = 0;
            String previous = "";
            Set<String> required = new HashSet<String>(contract.required.keySet());
            try (BufferedReader rows = reader(index)) {
                String line;
                while ((line = rows.readLine()) != null) {
                    Row r = new Row(line);
                    if (r.path.compareTo(previous) <= 0) throw new IOException("Duplicate or unsorted index path");
                    previous = r.path;
                    if (++entries > count || r.size > total - bytes) throw new IOException("Data index totals do not match");
                    bytes += r.size;
                    String requiredSha = contract.required.get(r.path);
                    if (requiredSha != null) {
                        if (!requiredSha.equals(r.sha)) throw new IOException("Required game input version mismatch: " + r.path);
                        required.remove(r.path);
                    }
                }
            }
            if (entries != count || bytes != total || !required.isEmpty()) throw new IOException("Incomplete data index");
            long done = 0;
            try (BufferedReader rows = reader(index)) {
                String line;
                while ((line = rows.readLine()) != null) {
                    cancelled(cancel);
                    Row r = new Row(line);
                    next(zip, "payload/" + r.path);
                    File file = new File(stage, r.path);
                    if (!file.getCanonicalPath().startsWith(stage.getCanonicalPath() + File.separator))
                        throw new IOException("Archive path escapes import directory");
                    File parent = file.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create data directory");
                    MessageDigest fileDigest = digest(); long written = 0;
                    try (FileOutputStream out = new FileOutputStream(file)) {
                        byte[] b = new byte[128 * 1024]; int n;
                        while ((n = zip.read(b)) != -1) {
                            cancelled(cancel);
                            if (n > r.size - written) throw new IOException("Data entry exceeds declared size: " + r.path);
                            out.write(b, 0, n); fileDigest.update(b, 0, n); written += n;
                            if (progress != null) progress.update(done + written, total, r.path);
                        }
                    }
                    if (written != r.size || !r.sha.equals(hexBytes(fileDigest.digest())))
                        throw new IOException("Data entry incomplete or corrupt: " + r.path);
                    done += written;
                }
            }
            if (zip.getNextEntry() != null) throw new IOException("Unexpected extra data entry");
            cancelled(cancel);
            String receipt = "bundle_id=" + id + "\nnative_sha256=" + contract.nativeSha
                    + "\nindex_sha256=" + contract.indexSha + "\npayload_bytes=" + total + "\n";
            syncWrite(new File(stage, "ready.properties"), receipt.getBytes(StandardCharsets.UTF_8));
            if (!stage.renameTo(destination)) throw new IOException("Cannot publish completed data directory");
            File active = new File(store, "active.properties");
            File pending = new File(store, ".hnl-active-" + UUID.randomUUID().toString());
            syncWrite(pending, receipt.getBytes(StandardCharsets.UTF_8));
            Files.move(pending.toPath(), active.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return new Installed(destination, id, total);
        } catch (IllegalArgumentException e) { throw new IOException("Invalid local data package", e); }
    }
}
