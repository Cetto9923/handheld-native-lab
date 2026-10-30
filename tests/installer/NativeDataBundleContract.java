import org.ffrs.diagnostic.NativeDataBundle;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.*;

/** Self-authored data only. Exercises actual extractor/filesystem transactions. */
public final class NativeDataBundleContract {
    static final String NATIVE = String.join("", Collections.nCopies(64, "a"));
    static int passed;
    static String sha(byte[] data) throws Exception {
        byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder s = new StringBuilder();
        for (byte b : d) s.append(String.format("%02x", b & 255));
        return s.toString();
    }
    static byte[] bytes(String s) { return s.getBytes(StandardCharsets.UTF_8); }
    static final class Fixture {
        final Map<String, byte[]> files = new TreeMap<String, byte[]>();
        final Map<String, String> required = new HashMap<String, String>();
        String index, header, id;
        NativeDataBundle.Contract contract;
        Fixture(String name, String special) throws Exception {
            files.put("main.nso", bytes("not a game; installer fixture main " + name));
            files.put("sdk.nso", bytes("not an SDK; fixture " + name));
            files.put("romfs/text data.txt", bytes("self-authored fixture data " + name));
            if (name.equals("cancel-mid")) files.put("romfs/text data.txt", new byte[1024 * 1024]);
            required.put("main.nso", sha(files.get("main.nso")));
            required.put("sdk.nso", sha(files.get("sdk.nso")));
            if (special != null) files.put(special, bytes("not allowed"));
            StringBuilder s = new StringBuilder(); long total = 0;
            for (Map.Entry<String, byte[]> e : files.entrySet()) {
                s.append(sha(e.getValue())).append('\t').append(e.getValue().length).append('\t').append(e.getKey()).append('\n');
                total += e.getValue().length;
            }
            index = s.toString();
            if (name.equals("duplicate")) { index += index.split("\n")[0] + "\n"; total += files.values().iterator().next().length; }
            if (name.equals("required-missing")) {
                index = index.replaceAll("(?m)^[^\n]*\tsdk\\.nso\n", "");
                total -= files.remove("sdk.nso").length;
            }
            String indexSha = sha(bytes(index));
            contract = new NativeDataBundle.Contract(NATIVE, indexSha, required);
            id = sha(bytes(indexSha + "\n" + NATIVE));
            header = "format=hnl-native-data/1\nabi=arm64-v8a\nbundle_id=" + id + "\nnative_sha256=" + NATIVE
                    + "\nindex_sha256=" + indexSha + "\nfile_count=" + index.split("\n").length
                    + "\npayload_bytes=" + total + "\n";
        }
        byte[] zip(String fault) throws Exception {
            ByteArrayOutputStream data = new ByteArrayOutputStream();
            try (ZipOutputStream z = new ZipOutputStream(data)) {
                String h = header;
                if (fault.equals("wrong-native")) h = h.replace("native_sha256=" + NATIVE, "native_sha256=" + String.join("", Collections.nCopies(64,"b")));
                if (fault.equals("bad-header")) h += "abi=arm64-v8a\n";
                put(z, "bundle.properties", bytes(h));
                put(z, "files.tsv", bytes(fault.equals("bad-index") ? index + "x" : index));
                for (Map.Entry<String, byte[]> e : files.entrySet()) {
                    byte[] body = e.getValue().clone();
                    if (fault.equals("corrupt") && e.getKey().equals("main.nso")) body[0] ^= 1;
                    put(z, "payload/" + e.getKey(), body);
                }
                if (fault.equals("extra")) put(z, "payload/unindexed", bytes("not indexed"));
            }
            byte[] result = data.toByteArray();
            return fault.equals("truncated") ? Arrays.copyOf(result, result.length / 2) : result;
        }
    }
    static void put(ZipOutputStream z, String path, byte[] data) throws Exception {
        z.putNextEntry(new ZipEntry(path)); z.write(data); z.closeEntry();
    }
    static void check(boolean v, String message) { if (!v) throw new AssertionError(message); }
    static void reject(File store, String name, String special, String fault) throws Exception {
        byte[] before = Files.readAllBytes(new File(store, "active.properties").toPath());
        Fixture f = new Fixture(name, special);
        boolean rejected = false;
        try { NativeDataBundle.install(new ByteArrayInputStream(f.zip(fault)), store, f.contract, null, null); }
        catch (IOException e) { rejected = true; }
        check(rejected, "accepted " + name);
        check(Arrays.equals(before, Files.readAllBytes(new File(store,"active.properties").toPath())), "active installation changed on " + name);
        check(!new File(store, f.id).exists(), "failed payload was published: " + name);
        passed++;
    }
    public static void main(String[] args) throws Exception {
        File store = new File(args[0]);
        Fixture a = new Fixture("valid", null);
        NativeDataBundle.Installed installed = NativeDataBundle.install(new ByteArrayInputStream(a.zip("")), store, a.contract, null, null);
        check(installed.root.isDirectory(), "missing published version");
        check(Arrays.equals(Files.readAllBytes(new File(installed.root,"main.nso").toPath()), a.files.get("main.nso")), "payload differs");
        passed++;
        reject(store, "corrupt", null, "corrupt");
        reject(store, "truncated", null, "truncated");
        reject(store, "extra", null, "extra");
        reject(store, "wrong-native", null, "wrong-native");
        reject(store, "bad-index", null, "bad-index");
        reject(store, "bad-header", null, "bad-header");
        reject(store, "duplicate", null, "");
        reject(store, "required-missing", null, "");
        reject(store, "traversal", "../escape", "");
        reject(store, "absolute", "/escape", "");
        reject(store, "windows", "C:\\escape", "");
        reject(store, "reserved", ".hnl-index.tsv", "");
        Fixture c = new Fixture("cancel", null);
        byte[] before = Files.readAllBytes(new File(store, "active.properties").toPath());
        boolean interrupted = false;
        try { NativeDataBundle.install(new ByteArrayInputStream(c.zip("")), store, c.contract, null, () -> true); }
        catch (InterruptedIOException e) { interrupted = true; }
        check(interrupted && Arrays.equals(before, Files.readAllBytes(new File(store,"active.properties").toPath())), "cancel changed active data");
        passed++;
        Fixture mid = new Fixture("cancel-mid", null);
        final boolean[] stop = { false };
        interrupted = false;
        try {
            NativeDataBundle.install(new ByteArrayInputStream(mid.zip("")), store, mid.contract,
                    (done, total, path) -> { if (path.equals("romfs/text data.txt")) stop[0] = true; }, () -> stop[0]);
        } catch (InterruptedIOException e) { interrupted = true; }
        check(interrupted && stop[0] && Arrays.equals(before, Files.readAllBytes(new File(store,"active.properties").toPath())), "mid-copy cancellation changed active data");
        check(!new File(store, mid.id).exists(), "cancelled payload was published");
        passed++;
        NativeDataBundle.Installed resumed = NativeDataBundle.install(new ByteArrayInputStream(mid.zip("")), store, mid.contract, null, null);
        check(Files.size(new File(resumed.root,"romfs/text data.txt").toPath()) == 1024 * 1024, "resume lost partial data");
        passed++;
        Fixture b = new Fixture("upgrade", null);
        NativeDataBundle.Installed upgraded = NativeDataBundle.install(new ByteArrayInputStream(b.zip("")), store, b.contract, null, null);
        check(!upgraded.id.equals(installed.id) && installed.root.isDirectory(), "upgrade removed old version");
        check(new String(Files.readAllBytes(new File(store,"active.properties").toPath()), StandardCharsets.UTF_8).contains(upgraded.id), "active pointer not updated");
        passed++;
        NativeDataBundle.Installed rollback = NativeDataBundle.recover(store, a.contract, null, null);
        check(rollback.id.equals(installed.id) && upgraded.root.isDirectory(), "rollback lost prior version");
        check(new String(Files.readAllBytes(new File(store,"active.properties").toPath()), StandardCharsets.UTF_8).contains(installed.id), "rollback pointer wrong");
        passed++;
        NativeDataBundle.Installed repeated = NativeDataBundle.install(new ByteArrayInputStream(a.zip("")), store, a.contract, null, null);
        check(repeated.id.equals(installed.id), "same version recovery failed"); passed++;
        File modified = new File(upgraded.root, "main.nso");
        byte[] tampered = Files.readAllBytes(modified.toPath()); tampered[0] ^= 1;
        Files.write(modified.toPath(), tampered);
        before = Files.readAllBytes(new File(store, "active.properties").toPath());
        boolean refused = false;
        try { NativeDataBundle.recover(store, b.contract, null, null); } catch (IOException e) { refused = true; }
        check(refused && Arrays.equals(before, Files.readAllBytes(new File(store,"active.properties").toPath())), "corrupt recovery changed active data");
        passed++;
        check(!new File(store.getParentFile(), "escape").exists(), "path escaped store");
        System.out.println("actual extractor contracts passed=" + passed + "; all fixture content self-authored, no game/runtime acceptance");
    }
}
