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
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Restores guest save files from a previous export in Download into the game's save root.
 */
final class SaveRestorer {
    private static final String TAG = "FFRS-SaveRestore";

    /** Save file names the guest writes below the save root. */
    static final String[] SAVE_FILES = {
            "GameSaveData0", "GameSaveData1", "GameSaveData2",
            "OptionSaveData", "EnhancedInputUserSettings"
    };

    /** Result of one restore attempt, for the caller to report. */
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
            if (error != null) return "restore failed: " + error;
            return "restored " + fileCount + " file(s), " + totalBytes + " byte(s) from " + (directory != null ? directory.getName() : "unknown");
        }
    }

    private SaveRestorer() {}

    /**
     * Finds all FFRS-save-* directories in the public Download folder, sorted newest first.
     */
    static List<File> listBackupDirectories() {
        List<File> list = new ArrayList<File>();
        try {
            File download = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            if (download != null && download.isDirectory()) {
                File[] children = download.listFiles();
                if (children != null) {
                    for (File f : children) {
                        if (f.isDirectory() && f.getName().startsWith("FFRS-save-")) {
                            list.add(f);
                        }
                    }
                    Collections.sort(list, new Comparator<File>() {
                        @Override public int compare(File a, File b) {
                            return Long.compare(b.lastModified(), a.lastModified());
                        }
                    });
                }
            }
        } catch (Throwable t) {
            Log.w(TAG, "listBackupDirectories failed: " + t.getMessage());
        }
        return list;
    }

    /**
     * Restores save files from backupDir into targetDir (liveSaveRoot).
     */
    static Result restore(Context context, File backupDir, File targetDir) {
        if (context == null) return new Result(backupDir, 0, 0, "no context");
        if (backupDir == null || !backupDir.isDirectory())
            return new Result(backupDir, 0, 0, "backup directory not found: " + backupDir);
        if (targetDir == null)
            return new Result(backupDir, 0, 0, "target save directory is null");

        if (!targetDir.isDirectory() && !targetDir.mkdirs()) {
            return new Result(backupDir, 0, 0, "cannot create target save directory: " + targetDir.getAbsolutePath());
        }

        try {
            int count = 0;
            long bytes = 0;
            for (String name : SAVE_FILES) {
                File source = new File(backupDir, name);
                if (!source.isFile()) continue;
                File target = new File(targetDir, name);
                long size = copyFile(source, target);
                if (size < 0) continue;
                count++;
                bytes += size;
            }
            if (count == 0) {
                return new Result(backupDir, 0, 0, "no valid save files found in " + backupDir.getName());
            }
            Log.i(TAG, "restore complete: count=" + count + " bytes=" + bytes + " from=" + backupDir.getAbsolutePath());
            return new Result(backupDir, count, bytes, null);
        } catch (Throwable t) {
            Log.e(TAG, "restore failed", t);
            return new Result(backupDir, 0, 0, String.valueOf(t.getMessage()));
        }
    }

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
}
