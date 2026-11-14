import org.ffrs.diagnostic.NativeDataBundle;
import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Self-authored bytes only; tests staging transactions, not GPU pipeline validity. */
public final class PipelineWarmSeedContract {
    static int passed;
    static void check(boolean ok, String message) { if (!ok) throw new AssertionError(message); }
    static String sha(byte[] bytes) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(bytes))
            result.append(String.format(Locale.ROOT, "%02x", b & 255));
        return result.toString();
    }
    static NativeDataBundle.Contract contract(byte[] bytes) throws Exception {
        String zero = String.join("", Collections.nCopies(64, "0"));
        Map<String, String> required = new HashMap<String, String>();
        if (bytes != null) required.put("pipeline-warm-list.bin", sha(bytes));
        return new NativeDataBundle.Contract(zero, zero, required);
    }
    interface Action { void run() throws Exception; }
    static void refuse(Action action) throws Exception {
        boolean failed = false;
        try { action.run(); } catch (IOException expected) { failed = true; }
        check(failed, "invalid seed accepted");
    }
    static byte[] apkSeed() throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream output = new DataOutputStream(bytes);
        int[] header = {0x57504c58, 1, 16};
        for (int value : header) output.writeInt(Integer.reverseBytes(value));
        output.write("0123456789abcdef".getBytes("US-ASCII"));
        int[] words = {10, 1, 0, 0, 0, 0, 1, 0, 16, 0x2e171b5f, 0xbfad94ed};
        for (int value : words) output.writeInt(Integer.reverseBytes(value));
        return bytes.toByteArray();
    }
    static final class OwnedInput extends ByteArrayInputStream {
        boolean closed;
        OwnedInput(byte[] bytes) { super(bytes); }
        @Override public void close() throws IOException { closed = true; super.close(); }
    }
    static void noApkTemps(File runtime) {
        for (String name : runtime.list())
            check(!name.startsWith(".apk-warm-seed-"), "APK temporary file leaked");
    }
    static void apkContracts(File root) throws Exception {
        final byte[] good = apkSeed();
        final File legacy = new File(root, "legacy-runtime");
        final File managed = new File(root, "managed/runtime/version-fixture");
        final byte[] learned = {41, 42, 43}, dataSeed = {51, 52, 53};
        for (File runtime : new File[]{legacy, managed}) {
            check(runtime.mkdirs(), "APK runtime fixture");
            File shaders = new File(runtime, "shaders"); check(shaders.mkdir(), "APK shader fixture");
            File history = new File(runtime, "vk-pipeline-list.bin");
            File bundled = new File(shaders, "vk-pipeline-list.seed");
            Files.write(history.toPath(), learned); Files.write(bundled.toPath(), dataSeed);
            OwnedInput input = new OwnedInput(good);
            NativeDataBundle.stageApkPipelineWarmSeed(input, runtime);
            check(!input.closed, "stager closed the caller's stream"); input.close();
            File seed = new File(runtime, "apk-pipeline-warm.seed");
            check(Arrays.equals(Files.readAllBytes(seed.toPath()), good), "APK seed bytes differ");
            check(Arrays.equals(Files.readAllBytes(history.toPath()), learned)
                    && Arrays.equals(Files.readAllBytes(bundled.toPath()), dataSeed), "existing warm data changed");
            check(runtime.list().length == 3, "APK staging wrote an unexpected file"); noApkTemps(runtime); passed++;
            check(seed.setLastModified(1600000000000L), "fixture timestamp");
            NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(good), runtime);
            check(seed.lastModified() == 1600000000000L, "identical APK seed was rewritten"); passed++;
        }
        final File seed = new File(legacy, "apk-pipeline-warm.seed");
        byte[] updated = new byte[good.length * 2];
        System.arraycopy(good, 0, updated, 0, good.length);
        System.arraycopy(good, 0, updated, good.length, good.length);
        NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(updated), legacy);
        check(Arrays.equals(Files.readAllBytes(seed.toPath()), updated), "APK seed update not installed"); passed++;
        for (int length = 0; length < good.length; ++length) {
            final byte[] truncated = Arrays.copyOf(good, length);
            refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(truncated), legacy));
            check(Arrays.equals(Files.readAllBytes(seed.toPath()), updated), "truncation damaged APK seed");
            noApkTemps(legacy);
        }
        passed++;
        for (int offset : new int[]{0, 4, 8, 28}) {
            final byte[] malformed = good.clone();
            Arrays.fill(malformed, offset, offset + 4, (byte) 0xff);
            refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(malformed), legacy));
            check(Arrays.equals(Files.readAllBytes(seed.toPath()), updated), "malformed record damaged APK seed");
            noApkTemps(legacy);
        }
        byte[] tail = Arrays.copyOf(good, good.length + 1);
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(tail), legacy));
        noApkTemps(legacy); passed++;
        InputStream broken = new InputStream() {
            int reads;
            @Override public int read() throws IOException { throw new IOException("injected read failure"); }
            @Override public int read(byte[] b, int offset, int length) throws IOException {
                if (++reads > 1) throw new IOException("injected read failure");
                System.arraycopy(good, 0, b, offset, 11); return 11;
            }
        };
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(broken, legacy));
        check(Arrays.equals(Files.readAllBytes(seed.toPath()), updated), "I/O failure damaged APK seed");
        noApkTemps(legacy); passed++;
        InputStream oversized = new InputStream() {
            long remaining = 64L * 1024 * 1024;
            @Override public int read() { if (remaining == 0) return -1; --remaining; return 0; }
            @Override public int read(byte[] b, int offset, int length) {
                if (remaining == 0) return -1;
                int n = (int) Math.min(remaining, length); Arrays.fill(b, offset, offset + n, (byte) 0);
                remaining -= n; return n;
            }
        };
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(oversized, legacy));
        check(Arrays.equals(Files.readAllBytes(seed.toPath()), updated), "oversize damaged APK seed");
        noApkTemps(legacy); passed++;
        final File unavailable = new File(root, "not-a-runtime"); Files.write(unavailable.toPath(), learned);
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(good), unavailable));
        check(Arrays.equals(Files.readAllBytes(unavailable.toPath()), learned), "unavailable runtime modified"); passed++;
        final File moveFailure = new File(root, "move-failure"); check(moveFailure.mkdir(), "move fixture");
        File destination = new File(moveFailure, "apk-pipeline-warm.seed"); check(destination.mkdir(), "seed directory fixture");
        File sentinel = new File(destination, "keep"); Files.write(sentinel.toPath(), learned);
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(good), moveFailure));
        check(Arrays.equals(Files.readAllBytes(sentinel.toPath()), learned), "failed replace damaged destination");
        check(moveFailure.list().length == 1, "failed replace left extra files"); noApkTemps(moveFailure); passed++;
        refuse(() -> NativeDataBundle.stageApkPipelineWarmSeed(new ByteArrayInputStream(good), null)); passed++;
    }
    public static void main(String[] args) throws Exception {
        final File root = new File(args[0]);
        check(root.mkdirs(), "choose a fresh fixture directory");
        final File data = new File(root, "data"), runtime = new File(root, "runtime");
        check(data.mkdir() && runtime.mkdir(), "fixture directories");
        File shaders = new File(runtime, "shaders"); check(shaders.mkdir(), "shader directory");
        final File source = new File(data, "pipeline-warm-list.bin");
        final File seed = new File(shaders, "vk-pipeline-list.seed");
        File history = new File(runtime, "vk-pipeline-list.bin");
        byte[] good = new byte[512 * 1024]; Arrays.fill(good, (byte) 19);
        final NativeDataBundle.Contract c = contract(good);
        NativeDataBundle.stagePipelineWarmSeed(data, runtime, contract(null), null);
        check(shaders.list().length == 0, "absent optional seed wrote cache"); passed++;
        refuse(() -> NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null));
        check(!seed.exists(), "missing source created seed"); passed++;
        Files.write(source.toPath(), good); long stamp = source.lastModified();
        NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null);
        check(Arrays.equals(Files.readAllBytes(seed.toPath()), good), "copied seed differs");
        check(source.lastModified() == stamp && Arrays.equals(Files.readAllBytes(source.toPath()), good), "source was modified");
        check(shaders.list().length == 1, "temporary file leaked"); passed++;
        byte[] oldSeed = {7, 8, 9}; Files.write(seed.toPath(), oldSeed);
        byte[] bad = good.clone(); bad[0] ^= 1; Files.write(source.toPath(), bad);
        refuse(() -> NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null));
        check(Arrays.equals(Files.readAllBytes(seed.toPath()), oldSeed) && shaders.list().length == 1,
                "hash failure damaged old seed or leaked staging"); passed++;
        Files.write(source.toPath(), good);
        final int[] checks = {0}; boolean cancelled = false;
        try { NativeDataBundle.stagePipelineWarmSeed(data, runtime, c,
                new NativeDataBundle.Cancel() { public boolean requested() { return ++checks[0] == 2; } }); }
        catch (InterruptedIOException expected) { cancelled = true; }
        check(cancelled && Arrays.equals(Files.readAllBytes(seed.toPath()), oldSeed)
                && shaders.list().length == 1, "cancel damaged old seed or leaked staging"); passed++;
        cancelled = false;
        try { NativeDataBundle.stagePipelineWarmSeed(data, runtime, c,
                new NativeDataBundle.Cancel() { public boolean requested() {
                    for (File f : shaders.listFiles())
                        if (f.getName().startsWith(".warm-seed-") && f.length() == good.length) return true;
                    return false;
                } }); }
        catch (InterruptedIOException expected) { cancelled = true; }
        check(cancelled && Arrays.equals(Files.readAllBytes(seed.toPath()), oldSeed)
                && shaders.list().length == 1, "final staging cancellation was ignored"); passed++;
        Files.write(history.toPath(), new byte[]{42}); Files.write(source.toPath(), bad);
        NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null);
        check(Arrays.equals(Files.readAllBytes(history.toPath()), new byte[]{42})
                && Arrays.equals(Files.readAllBytes(seed.toPath()), oldSeed), "learned history was overwritten"); passed++;
        Files.delete(history.toPath()); Files.delete(source.toPath());
        try (RandomAccessFile sparse = new RandomAccessFile(source, "rw")) { sparse.setLength(64L * 1024 * 1024); }
        refuse(() -> NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null)); passed++;
        Files.delete(source.toPath());
        File linked = new File(root, "fixture-source"); Files.write(linked.toPath(), good);
        Files.createSymbolicLink(source.toPath(), linked.toPath());
        refuse(() -> NativeDataBundle.stagePipelineWarmSeed(data, runtime, c, null));
        check(Arrays.equals(Files.readAllBytes(linked.toPath()), good), "linked source changed"); passed++;
        apkContracts(root);
        System.out.println("pipeline seed staging contracts passed=" + passed + "; fixture only, no GPU/gameplay acceptance");
    }
}
