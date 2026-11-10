package org.ffrs.diagnostic;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;

/** Creates an empty app-private save root for explicitly requested debug runs. */
final class RegressionSaveRoot {
    static String select(String original, boolean requested, boolean debuggable, File appFiles,
            String resumeName)
            throws IOException {
        if (!requested) return original;
        if (!debuggable) throw new IOException("isolated regression save requires a debug build");
        if (resumeName != null) {
            if (!resumeName.matches("shield-regression-[0-9]+"))
                throw new IOException("invalid regression save name");
            Path candidate = appFiles.toPath().resolve(resumeName);
            if (!Files.isDirectory(candidate, LinkOption.NOFOLLOW_LINKS)
                    || !candidate.toFile().getCanonicalFile().getParentFile()
                            .equals(appFiles.getCanonicalFile()))
                throw new IOException("regression save directory unavailable");
            return candidate.toFile().getAbsolutePath();
        }
        return Files.createTempDirectory(appFiles.toPath(), "shield-regression-").toFile()
                .getAbsolutePath();
    }
    private RegressionSaveRoot() {}
}
