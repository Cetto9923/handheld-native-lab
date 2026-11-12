package org.ffrs.diagnostic;

import android.util.Log;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;

/**
 * Read and write access to the guest's memory.
 *
 * The Switch guest runs as a thread inside this app process, so the pages it
 * owns are part of our own address space. /proc/self/smaps lists the regions
 * with their resident size and /proc/self/mem gives read and write access to
 * them. This is the only way to change the values the running game holds:
 * editing the save file only changes what the next load reads.
 *
 * The scan walks the resident pages only. The guest reserves a very large
 * virtual range and most of it is untouched, so scanning every mapped page
 * would read gigabytes of zero pages and take minutes.
 */
public final class GuestMemory {
    private static final String TAG = "FFRS-GuestMem";
    private GuestMemory() {}

    /** One mapped region of this process. */
    public static final class Region {
        public final long start, end;
        public final long resident;
        public final String name;
        Region(long s, long e, long r, String n) { start = s; end = e; resident = r; name = n; }
        @Override public String toString() {
            return String.format("%016x-%016x rss=%dK %s", start, end, resident / 1024, name);
        }
    }

    /**
     * Parses /proc/self/smaps and keeps the regions that have resident pages.
     *
     * smaps repeats the address header for every mapping entry, so a new
     * header line starts a new region; the Rss line arrives inside it.
     */
    public static List<Region> regions() {
        List<Region> out = new ArrayList<>();
        long start = 0, end = 0, rss = 0;
        String name = "";
        boolean inRegion = false;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new FileInputStream("/proc/self/smaps"), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.length() > 8 && Character.digit(line.charAt(0), 16) >= 0 && line.indexOf('-') > 0) {
                    if (inRegion && rss > 0) out.add(new Region(start, end, rss, name));
                    inRegion = false; rss = 0; name = "";
                    String[] t = line.split("\\s+", 6);
                    try {
                        String[] se = t[0].split("-");
                        start = Long.parseUnsignedLong(se[0], 16);
                        end = Long.parseUnsignedLong(se[1], 16);
                        name = t.length > 5 ? t[5] : "";
                    } catch (Exception ex) { inRegion = false; continue; }
                    inRegion = true;
                    continue;
                }
                if (!inRegion) continue;
                if (line.startsWith("Rss:")) {
                    String[] t = line.split("\\s+");
                    try { rss = Long.parseLong(t[1]) * 1024L; } catch (Exception ignored) { }
                }
            }
            if (inRegion && rss > 0) out.add(new Region(start, end, rss, name));
        } catch (IOException e) {
            Log.w(TAG, "smaps read failed: " + e.getMessage());
        }
        return out;
    }

    /** Searches the resident memory for a byte pattern; returns the addresses found. */
    public static long[] find(byte[] pattern, int maxHits) {
        long[] none = new long[0];
        if (pattern == null || pattern.length == 0) return none;
        long started = System.currentTimeMillis();
        List<Long> hits = new ArrayList<>();
        final int chunk = 1 << 20;
        byte[] buf = new byte[chunk + pattern.length];
        long scanned = 0;
        try (RandomAccessFile mem = new RandomAccessFile("/proc/self/mem", "r")) {
            for (Region r : regions()) {
                if (r.end - r.start < pattern.length) continue;
                long pos = r.start;
                while (pos < r.end) {
                    int want = (int) Math.min(chunk, r.end - pos);
                    int got;
                    try {
                        mem.seek(pos);
                        got = mem.read(buf, 0, want);
                    } catch (IOException e) { got = -1; }
                    if (got <= 0) break;
                    scanned += got;
                    int idx = indexOf(buf, got, pattern, 0);
                    while (idx >= 0) {
                        hits.add(pos + idx);
                        if (hits.size() >= maxHits) {
                            Log.i(TAG, "find done hits=" + hits.size() + " scanned=" + (scanned >> 10) + "K ms="
                                    + (System.currentTimeMillis() - started));
                            return toArray(hits);
                        }
                        idx = indexOf(buf, got, pattern, idx + 1);
                    }
                    if (got < want) break;
                    int keep = pattern.length - 1;
                    System.arraycopy(buf, got - keep, buf, 0, keep);
                    pos += got - keep;
                }
            }
        } catch (IOException e) {
            Log.w(TAG, "mem scan failed: " + e.getMessage());
        }
        Log.i(TAG, "find done hits=" + hits.size() + " scanned=" + (scanned >> 10) + "K ms="
                + (System.currentTimeMillis() - started));
        return toArray(hits);
    }

    /** Reads bytes at a process address. */
    public static byte[] peek(long address, int length) {
        if (length <= 0) return null;
        byte[] out = new byte[length];
        try (RandomAccessFile mem = new RandomAccessFile("/proc/self/mem", "r")) {
            mem.seek(address);
            mem.readFully(out);
        } catch (Exception e) {
            Log.w(TAG, "peek failed at " + Long.toHexString(address) + ": " + e.getMessage());
            return null;
        }
        return out;
    }

    /** Writes bytes at a process address. */
    public static boolean poke(long address, byte[] data) {
        if (data == null || data.length == 0) return false;
        try (RandomAccessFile mem = new RandomAccessFile("/proc/self/mem", "rw")) {
            mem.seek(address);
            mem.write(data);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "poke failed at " + Long.toHexString(address) + ": " + e.getMessage());
            return false;
        }
    }

    /**
     * Receives the chunks of a single guest memory walk.
     *
     * The scan needs several answers out of one pass over the memory, and the
     * callback keeps the walk independent of what is being looked for.
     */
    public interface ChunkSink {
        /** Called with the address of the chunk and the bytes that were read. */
        void onChunk(long address, byte[] data, int length);
    }

    /**
     * Walks the resident guest memory once and hands every chunk to the sink.
     *
     * One walk is far cheaper than one walk per question: the guest holds
     * hundreds of megabytes of resident pages.
     */
    public static void walk(ChunkSink sink) {
        if (sink == null) return;
        final int chunk = 1 << 22;
        byte[] buf = new byte[chunk];
        long scanned = 0;
        long started = System.currentTimeMillis();
        try (RandomAccessFile mem = new RandomAccessFile("/proc/self/mem", "r")) {
            for (Region r : regions()) {
                long pos = r.start;
                while (pos < r.end) {
                    int want = (int) Math.min(chunk, r.end - pos);
                    int got;
                    try { mem.seek(pos); got = mem.read(buf, 0, want); } catch (IOException e) { got = -1; }
                    if (got <= 0) break;
                    scanned += got;
                    sink.onChunk(pos, buf, got);
                    if (got < want) break;
                    pos += got;
                }
            }
        } catch (IOException e) {
            Log.w(TAG, "mem walk failed: " + e.getMessage());
        }
        Log.i(TAG, "walk done scanned=" + (scanned >> 10) + "K ms=" + (System.currentTimeMillis() - started));
    }

    private static long[] toArray(List<Long> v) {
        long[] out = new long[v.size()];
        for (int i = 0; i < out.length; i++) out[i] = v.get(i);
        return out;
    }

    /**
     * The first position of the needle in the buffer, or -1.
     *
     * The search stays on the bytes: turning a four megabyte chunk into a
     * string for every needle costs more time than the walk itself.
     */
    private static int indexOf(byte[] hay, int len, byte[] needle, int from) {
        final int n = needle.length;
        final int last = len - n;
        final byte first = needle[0];
        for (int i = from; i <= last; i++) {
            if (hay[i] != first) continue;
            int j = 1;
            while (j < n && hay[i + j] == needle[j]) j++;
            if (j == n) return i;
        }
        return -1;
    }

    /** ISO-8859-1 keeps every byte value, so byte patterns stay byte exact. */
    private static String latin1(byte[] b) {
        try {
            return new String(b, "ISO-8859-1");
        } catch (IOException e) {
            return "";
        }
    }
}
