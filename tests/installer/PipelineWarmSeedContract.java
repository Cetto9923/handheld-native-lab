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
        System.out.println("pipeline seed staging contracts passed=" + passed + "; fixture only, no GPU/gameplay acceptance");
    }
}
