import org.ffrs.diagnostic.NativeDataBundle;
import java.io.*;
import java.nio.file.Files;
import java.util.*;

/** Actual directory metadata + seed staging, using a host-pinned APK contract. No GPU execution. */
public final class DirectoryWarmSeedInterop {
    public static void main(String[] args) throws Exception {
        Properties trusted = new Properties();
        try (InputStream in = new FileInputStream(args[2])) { trusted.load(in); }
        Map<String, String> required = new HashMap<String, String>();
        for (String key : trusted.stringPropertyNames())
            if (key.startsWith("required.")) required.put(key.substring(9), trusted.getProperty(key));
        if (!required.containsKey("pipeline-warm-list.bin")) throw new AssertionError("missing pinned seed");
        NativeDataBundle.Contract contract = new NativeDataBundle.Contract(
                trusted.getProperty("native_sha256"), trusted.getProperty("index_sha256"), required);
        File data = new File(args[0]), runtime = new File(args[1]);
        if (!runtime.mkdirs() || !new File(runtime, "shaders").mkdir())
            throw new AssertionError("use a fresh host runtime output");
        // Exporter separately hashes every payload. This step checks the actual
        // shipped index/receipt/lengths plus a fresh SHA while staging the seed.
        NativeDataBundle.Installed installed = NativeDataBundle.verifyDirectory(data, contract, false, null, null);
        NativeDataBundle.stagePipelineWarmSeed(data, runtime, contract, null);
        File copied = new File(runtime, "shaders/vk-pipeline-list.seed");
        if (!Arrays.equals(Files.readAllBytes(copied.toPath()),
                           Files.readAllBytes(new File(data, "pipeline-warm-list.bin").toPath())))
            throw new AssertionError("seed staging bytes differ");
        System.out.println("Actual directory index/length and pinned seed staging verified: payload_bytes="
                + installed.bytes + ", seed_bytes=" + copied.length() + "; HOST_ONLY_NOT_ANDROID_OR_GAME_ACCEPTANCE");
    }
}
