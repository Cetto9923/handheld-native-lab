package org.ffrs.diagnostic;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class RegressionSaveRootContract {
    private static void check(boolean condition) {
        if (!condition) throw new AssertionError();
    }
    public static void main(String[] args) throws Exception {
        Path base = Files.createDirectory(new File(args[0], "fixture").toPath());
        Path original = Files.createDirectory(base.resolve("original"));
        Path app = Files.createDirectory(base.resolve("app"));
        byte[] sentinel = "original save sentinel".getBytes(StandardCharsets.UTF_8);
        Files.write(original.resolve("save"), sentinel);
        String root = original.toString();
        check(RegressionSaveRoot.select(root, false, false, app.toFile(), null).equals(root));
        check(RegressionSaveRoot.select(null, false, false, app.toFile(), null) == null);
        try {
            RegressionSaveRoot.select(root, true, false, app.toFile(), null);
            throw new AssertionError("non-debug request accepted");
        } catch (IOException expected) { }
        Path first = new File(RegressionSaveRoot.select(root, true, true, app.toFile(), null)).toPath();
        Path second = new File(RegressionSaveRoot.select(root, true, true, app.toFile(), null)).toPath();
        check(!first.equals(second) && first.getParent().equals(app) && second.getParent().equals(app));
        check(!first.equals(original) && !second.equals(original));
        Files.write(first.resolve("save"), new byte[] { 1, 2, 3 });
        check(RegressionSaveRoot.select(root, true, true, app.toFile(), first.getFileName().toString())
                .equals(first.toString()));
        try {
            RegressionSaveRoot.select(root, true, true, app.toFile(), "../original");
            throw new AssertionError("path traversal accepted");
        } catch (IOException expected) { }
        try {
            RegressionSaveRoot.select(root, true, true, original.resolve("save").toFile(), null);
            throw new AssertionError("invalid app directory accepted");
        } catch (IOException expected) { }
        check(Arrays.equals(Files.readAllBytes(original.resolve("save")), sentinel));
        System.out.println("PASS: default roots preserved; isolated roots fresh; failures abort; sentinel unchanged");
    }
}
