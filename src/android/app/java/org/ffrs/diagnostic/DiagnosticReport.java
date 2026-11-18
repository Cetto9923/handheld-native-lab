package org.ffrs.diagnostic;

import java.io.File;
import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Bounded, read-only diagnostic ZIP writer. This class has no Android dependencies. */
final class DiagnosticReport {
    static final long MAX_ARCHIVE_BYTES = 16L * 1024 * 1024;
    static final int HEAD_BYTES = 64 * 1024;
    static final int TAIL_BYTES = 2 * 1024 * 1024;
    static final int MAX_TRACE_BYTES = 256 * 1024;
    private static final long CONTENT_BUDGET = 15L * 1024 * 1024;
    private static final int MAX_TEXT_BYTES = 64 * 1024;
    private static final String[] LOG_NAMES = {
            "pipeline-failures.log", "guest_boot.log", "guest_boot.log.prev", "engine.log.fatal",
            "engine.log", "engine.log.1", "engine.log.prev"
    };

    static final class Trace {
        final InputStream input;
        final String unavailable;

        Trace(InputStream input, String unavailable) {
            this.input = input;
            this.unavailable = unavailable;
        }
    }

    static final class Result {
        final int fileCount;
        final long totalBytes;
        final String error;

        Result(int fileCount, long totalBytes, String error) {
            this.fileCount = fileCount;
            this.totalBytes = totalBytes;
            this.error = error;
        }

        boolean ok() { return error == null; }

        String describe() {
            return ok() ? "已导出 " + fileCount + " 个文件，共 " + totalBytes + " 字节"
                    : "导出失败：" + error;
        }
    }

    private final ZipOutputStream zip;
    private final StringBuilder manifest = new StringBuilder();
    private long remaining = CONTENT_BUDGET;
    private int fileCount;

    private DiagnosticReport(OutputStream output) {
        zip = new ZipOutputStream(output);
        manifest.append("FFRS diagnostic report\n")
                .append("format=1\narchive_limit_bytes=").append(MAX_ARCHIVE_BYTES)
                .append("\nlog_head_limit_bytes=").append(HEAD_BYTES)
                .append("\nlog_tail_limit_bytes=").append(TAIL_BYTES)
                .append("\ntrace_limit_bytes=").append(MAX_TRACE_BYTES)
                .append("\nLogs are sampled while the application may be writing them.\n")
                .append("Offsets refer to the file length observed when opened.\n\n");
    }

    /** Owns and closes the destination and all supplied trace streams, including on failure. */
    static Result write(OutputStream output, String metadata, String exitHistory,
                        File externalFiles, File runtimeRoot, List<Trace> traces) {
        if (output == null) {
            closeTraces(traces);
            return new Result(0, 0, "无法打开目标文件");
        }
        BoundedOutput destination = new BoundedOutput(output);
        DiagnosticReport report = new DiagnosticReport(destination);
        try (ZipOutputStream ignored = report.zip) {
            report.text("metadata.txt", metadata);
            report.text("exit-history.txt", exitHistory);
            report.traces(traces);
            report.logs(externalFiles, "logs/external");
            report.runtimeLogs(runtimeRoot);
            report.text("collection.txt", report.manifest.toString());
        } catch (IOException | SecurityException e) {
            return new Result(report.fileCount, destination.count, message(e));
        } finally {
            closeTraces(traces);
        }
        return new Result(report.fileCount, destination.count, null);
    }

    private void text(String name, String value) throws IOException {
        String text = value == null ? "unavailable\n" : value;
        byte[] bytes = text.substring(0, Math.min(text.length(), MAX_TEXT_BYTES)).getBytes(StandardCharsets.UTF_8);
        int length = Math.min(bytes.length, MAX_TEXT_BYTES);
        zip.putNextEntry(new ZipEntry(name));
        zip.write(bytes, 0, length);
        zip.closeEntry();
        remaining -= length;
        fileCount++;
        if (length < bytes.length || text.length() > MAX_TEXT_BYTES) manifest.append(name).append(" truncated=true\n");
    }

    private void traces(List<Trace> traces) throws IOException {
        if (traces == null || traces.isEmpty()) {
            manifest.append("system_trace unavailable=no trace records supplied\n");
            return;
        }
        for (int i = 0; i < Math.min(8, traces.size()); i++) {
            Trace trace = traces.get(i);
            String name = "traces/exit-" + (i + 1) + ".txt";
            if (trace == null || trace.input == null) {
                manifest.append(name).append(" unavailable=")
                        .append(trace == null ? "no stream" : oneLine(trace.unavailable)).append('\n');
                continue;
            }
            zip.putNextEntry(new ZipEntry(name));
            long copied = 0;
            String error = null;
            boolean truncated = false;
            byte[] buffer = new byte[16 * 1024];
            while (copied < MAX_TRACE_BYTES) {
                int count;
                try {
                    count = trace.input.read(buffer, 0, (int)Math.min(buffer.length, MAX_TRACE_BYTES - copied));
                } catch (IOException | SecurityException e) {
                    error = message(e);
                    break;
                }
                if (count <= 0) break;
                zip.write(buffer, 0, count);
                copied += count;
            }
            if (copied == MAX_TRACE_BYTES) {
                try { truncated = trace.input.read() >= 0; }
                catch (IOException | SecurityException e) { error = message(e); }
            }
            zip.closeEntry();
            remaining -= copied;
            fileCount++;
            manifest.append(name).append(" copied_bytes=").append(copied)
                    .append(" truncated=").append(truncated);
            if (error != null) manifest.append(" read_error=").append(oneLine(error));
            manifest.append('\n');
        }
    }

    private void runtimeLogs(File runtimeRoot) throws IOException {
        if (runtimeRoot == null) {
            manifest.append("logs/runtime unavailable=no runtime directory\n");
            return;
        }
        File root;
        File[] children;
        try {
            root = runtimeRoot.getCanonicalFile();
            children = root.listFiles();
        } catch (IOException | SecurityException e) {
            manifest.append("logs/runtime unavailable=").append(oneLine(message(e))).append('\n');
            return;
        }
        if (children == null) {
            manifest.append("logs/runtime unavailable=directory missing or unreadable\n");
            return;
        }
        List<File> versions = new ArrayList<File>();
        for (File child : children) {
            if (child.getName().matches("[A-Za-z0-9][A-Za-z0-9._-]{0,127}") && directDirectory(root, child))
                versions.add(child);
        }
        Collections.sort(versions, new RecentFirst(versions));
        manifest.append("runtime_directories_found=").append(versions.size())
                .append(" selected=").append(Math.min(4, versions.size())).append('\n');
        for (int i = 0; i < Math.min(4, versions.size()); i++) {
            File dir = versions.get(i);
            logs(dir, "logs/runtime/" + dir.getName());
        }
    }

    private void logs(File directory, String prefix) throws IOException {
        if (directory == null) {
            manifest.append(prefix).append(" unavailable=no directory\n");
            return;
        }
        File root;
        try {
            root = directory.getCanonicalFile();
            if (!root.isDirectory()) {
                manifest.append(prefix).append(" unavailable=directory missing\n");
                return;
            }
        } catch (IOException | SecurityException e) {
            manifest.append(prefix).append(" unavailable=").append(oneLine(message(e))).append('\n');
            return;
        }
        for (String logName : LOG_NAMES) log(root, logName, prefix + "/" + logName);
    }

    private void log(File root, String logName, String entry) throws IOException {
        File source = new File(root, logName);
        RandomAccessFile input;
        try {
            if (!source.getCanonicalFile().equals(source) || !source.isFile()) {
                manifest.append(entry).append(" unavailable=missing or unsafe file\n");
                return;
            }
            input = new RandomAccessFile(source, "r");
        } catch (IOException | SecurityException e) {
            manifest.append(entry).append(" unavailable=").append(oneLine(message(e))).append('\n');
            return;
        }
        try (RandomAccessFile file = input) {
            long length = file.length();
            long modified = source.lastModified();
            long allowed = Math.min(length, Math.min(HEAD_BYTES + TAIL_BYTES, remaining));
            if (allowed == 0 && length != 0) {
                manifest.append(entry).append(" skipped=archive content budget\n");
                return;
            }
            long head = allowed == length ? allowed : Math.min(HEAD_BYTES, allowed);
            long tail = allowed - head;
            zip.putNextEntry(new ZipEntry(entry));
            long copied = copyRange(file, 0, head);
            copied += copyRange(file, length - tail, tail);
            zip.closeEntry();
            remaining -= copied;
            fileCount++;
            manifest.append(entry).append(" observed_bytes=").append(length)
                    .append(" modified_ms=").append(modified)
                    .append(" copied_bytes=").append(copied)
                    .append(" head_range=[0,").append(head).append(") tail_range=[")
                    .append(length - tail).append(',').append(length).append(") truncated=")
                    .append(copied < length).append('\n');
        }
    }

    private long copyRange(RandomAccessFile source, long start, long length) throws IOException {
        if (length == 0) return 0;
        try { source.seek(start); }
        catch (IOException e) {
            manifest.append("source_read_error=").append(oneLine(message(e))).append('\n');
            return 0;
        }
        byte[] buffer = new byte[HEAD_BYTES];
        long copied = 0;
        while (copied < length) {
            int count;
            try { count = source.read(buffer, 0, (int)Math.min(buffer.length, length - copied)); }
            catch (IOException e) {
                manifest.append("source_read_error=").append(oneLine(message(e))).append('\n');
                break;
            }
            if (count < 0) break;
            zip.write(buffer, 0, count);
            copied += count;
        }
        return copied;
    }

    private static boolean directDirectory(File root, File child) {
        try { return child.isDirectory() && child.getCanonicalFile().equals(child)
                && root.equals(child.getParentFile()); }
        catch (IOException | SecurityException e) { return false; }
    }

    private static long recency(File directory) {
        long latest = directory.lastModified();
        for (String name : LOG_NAMES) {
            File file = new File(directory, name);
            try {
                if (file.isFile() && file.getCanonicalFile().equals(file)) latest = Math.max(latest, file.lastModified());
            } catch (IOException | SecurityException ignored) { }
        }
        return latest;
    }

    private static final class RecentFirst implements Comparator<File> {
        private final Map<File, Long> times = new HashMap<File, Long>();
        RecentFirst(List<File> directories) {
            for (File directory : directories) times.put(directory, recency(directory));
        }
        @Override public int compare(File first, File second) {
            int time = Long.compare(times.get(second), times.get(first));
            return time == 0 ? first.getName().compareTo(second.getName()) : time;
        }
    }

    private static void closeTraces(List<Trace> traces) {
        if (traces == null) return;
        for (Trace trace : traces) {
            if (trace != null && trace.input != null) {
                try { trace.input.close(); } catch (IOException ignored) { }
            }
        }
    }

    private static String message(Exception error) {
        String message = error.getMessage();
        return error.getClass().getSimpleName() + (message == null ? "" : " " + message);
    }

    private static String oneLine(String text) {
        if (text == null) return "unspecified";
        return text.replace('\n', ' ').replace('\r', ' ');
    }

    private static final class BoundedOutput extends FilterOutputStream {
        long count;
        BoundedOutput(OutputStream destination) { super(destination); }

        @Override public void write(int value) throws IOException {
            if (count >= MAX_ARCHIVE_BYTES) throw new IOException("诊断报告超过 16 MiB 上限");
            out.write(value);
            count++;
        }

        @Override public void write(byte[] buffer, int offset, int length) throws IOException {
            if (length > MAX_ARCHIVE_BYTES - count) throw new IOException("诊断报告超过 16 MiB 上限");
            out.write(buffer, offset, length);
            count += length;
        }
    }
}
