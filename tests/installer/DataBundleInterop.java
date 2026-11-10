import org.ffrs.diagnostic.NativeDataBundle;
import java.io.*;
import java.util.*;

/** Reads a Python-written package using the actual Java import core. */
public final class DataBundleInterop {
    public static void main(String[] args) throws Exception {
        Map<String, String> required = new HashMap<String, String>();
        required.put("main.nso", args[4]); required.put("sdk.nso", args[5]);
        NativeDataBundle.Contract c = new NativeDataBundle.Contract(args[2], args[3], required);
        NativeDataBundle.Installed result = NativeDataBundle.install(
                new FileInputStream(args[0]), new File(args[1]), c, null, null);
        if (!new File(result.root, "shaders/0123456789abcdef.xgs").isFile()
                || !new File(result.root, "romfs/FFRS/Content/Paks/FFRS-Switch.ucas").isFile())
            throw new AssertionError("missing fixture assets");
        System.out.println("Python -> Java import verified, bytes=" + result.bytes);
    }
}
