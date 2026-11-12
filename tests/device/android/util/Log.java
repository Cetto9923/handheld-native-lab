package android.util;

/**
 * Host side stub of the platform logger.
 *
 * The save editor logs a warning through android.util.Log. The host test
 * runs the real class, so it needs the class itself; the calls go to stdout
 * and never to the platform.
 */
public final class Log {
    private Log() {}

    public static int v(String tag, String msg) { return print("V", tag, msg, null); }
    public static int d(String tag, String msg) { return print("D", tag, msg, null); }
    public static int i(String tag, String msg) { return print("I", tag, msg, null); }
    public static int w(String tag, String msg) { return print("W", tag, msg, null); }
    public static int w(String tag, String msg, Throwable t) { return print("W", tag, msg, t); }
    public static int e(String tag, String msg) { return print("E", tag, msg, null); }
    public static int e(String tag, String msg, Throwable t) { return print("E", tag, msg, t); }

    private static int print(String level, String tag, String msg, Throwable t) {
        System.out.println(level + "/" + tag + ": " + msg);
        if (t != null) t.printStackTrace();
        return 0;
    }
}
