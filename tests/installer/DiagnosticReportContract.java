package org.ffrs.diagnostic;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Self-authored fixtures only. Exercises the actual read-only ZIP writer. */
public final class DiagnosticReportContract {
    private static final String[] LOGS = {"guest_boot.log", "guest_boot.log.prev", "engine.log", "engine.log.1", "engine.log.prev", "engine.log.fatal"};
    private static int passed;

    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static byte[] bytes(String text) { return text.getBytes(StandardCharsets.UTF_8); }

    private static Path put(Path directory, String name, byte[] content) throws IOException {
        Files.createDirectories(directory);
        return Files.write(directory.resolve(name), content);
    }

    private static Map<String, byte[]> unzip(byte[] archive) throws IOException {
        Map<String, byte[]> entries = new LinkedHashMap<String, byte[]>();
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(archive))) {
            ZipEntry entry;
            byte[] buffer = new byte[8192];
            while ((entry = zip.getNextEntry()) != null) {
                check(!entries.containsKey(entry.getName()), "duplicate ZIP entry");
                ByteArrayOutputStream output = new ByteArrayOutputStream();
                int read;
                while ((read = zip.read(buffer)) >= 0) output.write(buffer, 0, read);
                entries.put(entry.getName(), output.toByteArray());
            }
        }
        return entries;
    }

    private static Map<String, byte[]> export(File external, File runtime, List<DiagnosticReport.Trace> traces) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        DiagnosticReport.Result result = DiagnosticReport.write(output, "app_version=fixture\n", "unavailable: fixture\n", external, runtime, traces);
        check(result.ok(), result.describe());
        check(output.size() == result.totalBytes, "reported ZIP length differs");
        check(output.size() <= DiagnosticReport.MAX_ARCHIVE_BYTES, "archive exceeds 16 MiB");
        Map<String, byte[]> entries = unzip(output.toByteArray());
        check(entries.size() == result.fileCount, "reported entry count differs");
        return entries;
    }

    private static void whitelist(Path root) throws Exception {
        Path external = root.resolve("external"), runtime = root.resolve("runtime");
        for (String name : LOGS) put(external, name, bytes(name));
        put(external, "prod.keys", bytes("fixture key excluded"));
        put(external, "GameSaveData0", bytes("fixture save excluded"));
        put(external.resolve("romfs"), "engine.log", bytes("resource excluded"));
        put(external.resolve("nested"), "guest_boot.log", bytes("nested excluded"));
        for (int i = 0; i < 6; i++) {
            Path version = runtime.resolve("version-" + i);
            Path log = put(version, "guest_boot.log", bytes("version " + i));
            Files.setLastModifiedTime(log, FileTime.fromMillis(100000 + i * 1000));
            Files.setLastModifiedTime(version, FileTime.fromMillis(100000 + i * 1000));
        }
        put(runtime.resolve("invalid version"), "engine.log", bytes("invalid name excluded"));
        Path outside = root.resolve("outside");
        put(outside, "guest_boot.log", bytes("symlink excluded"));
        Files.createSymbolicLink(runtime.resolve("version-link"), outside);
        Files.createSymbolicLink(runtime.resolve("version-5").resolve("engine.log"), outside.resolve("guest_boot.log"));
        Map<String, byte[]> entries = export(external.toFile(), runtime.toFile(), Collections.<DiagnosticReport.Trace>emptyList());
        check(entries.size() == 13, "unexpected entries " + entries.keySet());
        for (String name : LOGS) check(Arrays.equals(entries.get("logs/external/" + name), bytes(name)), "whitelisted log missing " + name);
        for (int i = 2; i < 6; i++) check(entries.containsKey("logs/runtime/version-" + i + "/guest_boot.log"), "recent runtime missing");
        for (String name : entries.keySet()) {
            check(!name.contains("..") && !name.startsWith("/"), "unsafe ZIP path " + name);
            check(!name.contains("GameSave") && !name.contains("keys") && !name.contains("romfs")
                    && !name.contains("version-link") && !name.contains("invalid "), "non-whitelisted content included " + name);
            check(!name.startsWith("logs/runtime/version-0/") && !name.startsWith("logs/runtime/version-1/"), "old runtime included");
        }
        check(!entries.containsKey("logs/runtime/version-5/engine.log"), "symlink log followed");
        passed++;
    }

    private static void truncationAndSource(Path root) throws Exception {
        byte[] original = new byte[4 * 1024 * 1024];
        new Random(17).nextBytes(original);
        Path log = put(root, "engine.log", original);
        byte[] before = MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(log));
        FileTime modified = Files.getLastModifiedTime(log);
        Map<String, byte[]> entries = export(root.toFile(), null, null);
        byte[] got = entries.get("logs/external/engine.log");
        check(got.length == DiagnosticReport.HEAD_BYTES + DiagnosticReport.TAIL_BYTES, "log segment limit");
        check(Arrays.equals(Arrays.copyOfRange(got, 0, DiagnosticReport.HEAD_BYTES),
                Arrays.copyOfRange(original, 0, DiagnosticReport.HEAD_BYTES)), "head differs");
        check(Arrays.equals(Arrays.copyOfRange(got, DiagnosticReport.HEAD_BYTES, got.length),
                Arrays.copyOfRange(original, original.length - DiagnosticReport.TAIL_BYTES, original.length)), "tail differs");
        String manifest = new String(entries.get("collection.txt"), StandardCharsets.UTF_8);
        check(manifest.contains("observed_bytes=4194304") && manifest.contains("truncated=true")
                && manifest.contains("head_range=[0,65536)") && manifest.contains("tail_range=[2097152,4194304)"), "missing truncation metadata");
        check(Arrays.equals(before, MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(log))), "source content changed");
        check(modified.equals(Files.getLastModifiedTime(log)), "source modification time changed");
        passed++;
    }

    private static final class TrackedStream extends ByteArrayInputStream {
        boolean closed;
        TrackedStream(byte[] source) { super(source); }
        @Override public void close() throws IOException { closed = true; super.close(); }
    }

    private static void tracesAndUnavailable(Path root) throws Exception {
        List<DiagnosticReport.Trace> traces = new ArrayList<DiagnosticReport.Trace>();
        TrackedStream large = new TrackedStream(new byte[DiagnosticReport.MAX_TRACE_BYTES + 1]);
        traces.add(new DiagnosticReport.Trace(large, null));
        traces.add(new DiagnosticReport.Trace(null, "permission denied"));
        traces.add(new DiagnosticReport.Trace(new InputStream() {
            @Override public int read() throws IOException { throw new IOException("fixture read failure"); }
        }, null));
        for (int i = 3; i < 9; i++) traces.add(new DiagnosticReport.Trace(new TrackedStream(bytes("trace " + i)), null));
        Map<String, byte[]> entries = export(root.toFile(), null, traces);
        check(entries.get("traces/exit-1.txt").length == DiagnosticReport.MAX_TRACE_BYTES, "trace not bounded");
        check(!entries.containsKey("traces/exit-9.txt"), "too many traces exported");
        String manifest = new String(entries.get("collection.txt"), StandardCharsets.UTF_8);
        check(manifest.contains("truncated=true") && manifest.contains("permission denied")
                && manifest.contains("fixture read failure"), "trace failure not documented");
        check(large.closed && ((TrackedStream)traces.get(8).input).closed, "trace not closed");
        Map<String, byte[]> empty = export(null, null, null);
        check(empty.size() == 3, "empty report must keep metadata, history and collection manifest");
        check(new String(empty.get("collection.txt"), StandardCharsets.UTF_8).contains("unavailable"), "missing unavailable status");
        passed++;
    }

    private static void archiveBudget(Path root) throws Exception {
        byte[] content = new byte[3 * 1024 * 1024];
        new Random(29).nextBytes(content);
        Path external = root.resolve("external"), runtime = root.resolve("runtime");
        for (String name : LOGS) put(external, name, content);
        for (int i = 0; i < 4; i++) {
            for (String name : LOGS) put(runtime.resolve("runtime-" + i), name, content);
        }
        Map<String, byte[]> entries = export(external.toFile(), runtime.toFile(), null);
        String manifest = new String(entries.get("collection.txt"), StandardCharsets.UTF_8);
        check(manifest.contains("skipped=archive content budget"), "budget did not report skipped sources");
        long total = 0;
        for (byte[] value : entries.values()) total += value.length;
        check(total <= DiagnosticReport.MAX_ARCHIVE_BYTES, "uncompressed content exceeds archive limit");
        check(entries.containsKey("collection.txt"), "budget omitted collection manifest");
        passed++;
    }

    private static void destinationFailureAndRetry(Path root) throws Exception {
        final TrackedStream trace = new TrackedStream(bytes("trace"));
        DiagnosticReport.Result result = DiagnosticReport.write(new OutputStream() {
            int left = 200;
            @Override public void write(int value) throws IOException {
                if (--left <= 0) throw new IOException("fixture destination failure");
            }
        }, "metadata", "exits", root.toFile(), null,
                Collections.singletonList(new DiagnosticReport.Trace(trace, null)));
        check(!result.ok() && result.error.contains("fixture destination failure"), "destination error hidden");
        check(trace.closed, "failure leaked trace stream");
        TrackedStream noDestination = new TrackedStream(bytes("trace"));
        check(!DiagnosticReport.write(null, "", "", null, null,
                Collections.singletonList(new DiagnosticReport.Trace(noDestination, null))).ok(), "null destination accepted");
        check(noDestination.closed, "null destination leaked trace stream");
        check(export(root.toFile(), null, null).size() == 3, "retry did not succeed");
        passed++;
    }

    private static void liveFileShrink(Path root) throws Exception {
        final Path source = put(root, "guest_boot.log", new byte[3 * 1024 * 1024]);
        ByteArrayOutputStream output = new ByteArrayOutputStream() {
            boolean changed;
            @Override public synchronized void write(byte[] buffer, int offset, int length) {
                String text = new String(buffer, offset, length, StandardCharsets.UTF_8);
                if (!changed && text.equals("logs/external/guest_boot.log")) {
                    changed = true;
                    try { Files.write(source, bytes("new session")); }
                    catch (IOException e) { throw new AssertionError(e); }
                }
                super.write(buffer, offset, length);
            }
        };
        DiagnosticReport.Result result = DiagnosticReport.write(output, "", "", root.toFile(), null, null);
        check(result.ok(), "live log shrink failed " + result.describe());
        Map<String, byte[]> entries = unzip(output.toByteArray());
        check(Arrays.equals(entries.get("logs/external/guest_boot.log"), bytes("new session")), "shrunk file not sampled");
        check(new String(entries.get("collection.txt"), StandardCharsets.UTF_8).contains("truncated=true"), "live truncation not recorded");
        passed++;
    }

    public static void main(String[] args) throws Exception {
        Path root = new File(args[0]).toPath();
        whitelist(root.resolve("whitelist"));
        truncationAndSource(root.resolve("truncation"));
        tracesAndUnavailable(root.resolve("missing"));
        archiveBudget(root.resolve("budget"));
        destinationFailureAndRetry(root.resolve("retry"));
        liveFileShrink(root.resolve("live"));
        System.out.println("DiagnosticReportContract PASS " + passed);
    }
}
