package org.ffrs.diagnostic;

import android.content.Context;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Copies the guest save files into the shared Download collection.
 *
 * The guest writes its saves below the app-owned external files directory,
 * and the runtime creates that directory 0700 with 0600 files, so the
 * platform file manager cannot list or read them. A player who wants to
 * report a save problem therefore has no way to hand the file over: the
 * folder shows up as empty.
 *
 * This class copies the save files into a public Download subdirectory, so
 * the player finds them with the normal file manager and can send them the
 * same way they would send a photo. The copy is read-only with respect to
 * the source: the guest keeps owning and writing the original files.
 */
final class SaveExporter {
    private static final String TAG = "FFRS-SaveExport";

    /** Save file names the guest writes below the save root. */
    private static final String[] SAVE_FILES = {
            "GameSaveData0", "GameSaveData1", "GameSaveData2",
            "OptionSaveData", "EnhancedInputUserSettings"
    };

    /** Result of one export attempt, for the caller to report. */
    static final class Result {
        final File directory;
        final int fileCount;
        final long totalBytes;
        final String error;

        Result(File directory, int fileCount, long totalBytes, String error) {
            this.directory = directory;
            this.fileCount = fileCount;
            this.totalBytes = totalBytes;
            this.error = error;
        }

        boolean ok() { return error == null && fileCount > 0; }

        String describe() {
            if (error != null) return "export failed: " + error;
            return "exported " + fileCount + " file(s), " + totalBytes + " byte(s) to " + directory.getAbsolutePath();
        }
    }

    private SaveExporter() {}

    /**
     * Copies every save file that exists below {@code saveRoot} into a
     * timestamped directory below the public Download collection.
     *
     * Returns the result without throwing: a failed export must never take
     * the game down with it.
     */
    static Result export(Context context, File saveRoot) {
        if (context == null) return new Result(null, 0, 0, "no context");
        if (saveRoot == null || !saveRoot.isDirectory())
            return new Result(null, 0, 0, "save root is not a directory: " + saveRoot);
        try {
            File download = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            if (download == null) return new Result(null, 0, 0, "no public Download directory");
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
            File target = new File(download, "FFRS-save-" + stamp);
            if (!target.isDirectory() && !target.mkdirs())
                return new Result(null, 0, 0, "cannot create " + target.getAbsolutePath());
            int count = 0;
            long bytes = 0;
            for (String name : SAVE_FILES) {
                File source = new File(saveRoot, name);
                if (!source.isFile()) continue;
                long size = copyFile(source, new File(target, name));
                if (size < 0) continue;
                count++;
                bytes += size;
            }
            // A manifest makes the hand-over self-explanatory: the receiving
            // side knows which files are saves and which device produced them.
            writeManifest(new File(target, "README.txt"), context, saveRoot, count, bytes);
            Log.i(TAG, "export result: count=" + count + " bytes=" + bytes + " target=" + target.getAbsolutePath());
            return new Result(target, count, bytes, count == 0 ? "no save files found in " + saveRoot.getAbsolutePath() : null);
        } catch (Throwable t) {
            Log.e(TAG, "export failed", t);
            return new Result(null, 0, 0, String.valueOf(t.getMessage()));
        }
    }

    /** Returns the copied byte count, or -1 when the copy failed. */
    private static long copyFile(File source, File target) {
        try (InputStream in = new FileInputStream(source);
             OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[64 * 1024];
            long total = 0;
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                total += read;
            }
            out.flush();
            return total;
        } catch (IOException | SecurityException e) {
            Log.w(TAG, "copy failed for " + source.getName() + ": " + e.getMessage());
            return -1;
        }
    }

    private static void writeManifest(File manifest, Context context, File saveRoot, int count, long bytes) {
        try (OutputStream out = new FileOutputStream(manifest)) {
            String text = "FFRS save export\n"
                    + "created: " + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date()) + "\n"
                    + "source: " + saveRoot.getAbsolutePath() + "\n"
                    + "files: " + count + "\n"
                    + "bytes: " + bytes + "\n"
                    + "package: " + packageName(context) + "\n"
                    + "device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + "\n"
                    + "\nGameSaveDataN are the save slots, OptionSaveData holds the options\n"
                    + "and EnhancedInputUserSettings holds the input settings.\n";
            out.write(text.getBytes("UTF-8"));
            out.flush();
        } catch (IOException | SecurityException e) {
            Log.w(TAG, "manifest write failed: " + e.getMessage());
        }
    }

    /** The application id of the running app, or "unknown". */
    private static String packageName(Context context) {
        try {
            return context.getPackageName();
        } catch (Throwable t) {
            return "unknown";
        }
    }
}
