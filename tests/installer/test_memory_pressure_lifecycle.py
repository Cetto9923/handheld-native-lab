"""Compile the Activity's real pressure callbacks against a small JVM host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
JAVA = ROOT / 'src/android/app/java/org/ffrs/diagnostic'


class MemoryPressureLifecycle(unittest.TestCase):
    def test_actual_callbacks(self):
        source = (JAVA / 'GameActivity.java').read_text()
        start = source.rfind('@SuppressWarnings', 0, source.index('@Override public void onTrimMemory'))
        end = source.index('@Override protected void onStop()', start)
        callbacks = source[start:end]
        fixture = '''
class Activity {
    static final int TRIM_MEMORY_RUNNING_MODERATE=5, TRIM_MEMORY_RUNNING_CRITICAL=15;
    static final int TRIM_MEMORY_UI_HIDDEN=20, TRIM_MEMORY_BACKGROUND=40, TRIM_MEMORY_MODERATE=60;
    int parentCalls;
    public void onTrimMemory(int level) { ++parentCalls; }
    public void onLowMemory() { ++parentCalls; }
}
class Log { static void i(String tag, String message) {} }
class GameWindowNative {
    static int requested;
    static void requestMemoryTrim(int severity) { requested=severity; }
}
public class MemoryCallbacks extends Activity {
    boolean guestStarted=true;
    static final String TAG="test";
''' + callbacks + '''
    public static void main(String[] args) {
        MemoryCallbacks activity=new MemoryCallbacks();
        int[] levels={0,4,5,9,10,14,15,19,20,25,39,40,59,60,80};
        int[] expected={0,0,1,1,1,1,2,2,0,0,0,1,1,2,2};
        for(int index=0;index<levels.length;++index) {
            GameWindowNative.requested=0;
            activity.onTrimMemory(levels[index]);
            if(GameWindowNative.requested!=expected[index]) throw new AssertionError("level "+levels[index]);
        }
        activity.onLowMemory();
        if(GameWindowNative.requested!=2) throw new AssertionError("low memory");
        activity.guestStarted=false;
        GameWindowNative.requested=0;
        activity.onTrimMemory(80);
        activity.onLowMemory();
        if(GameWindowNative.requested!=0) throw new AssertionError("guest has not started");
        if(activity.parentCalls!=18) throw new AssertionError("super callback");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source_file = path / 'MemoryCallbacks.java'
            source_file.write_text(fixture)
            subprocess.run(['javac', str(source_file)], check=True, capture_output=True, text=True)
            subprocess.run(['java', '-cp', str(path), 'MemoryCallbacks'], check=True)


if __name__ == '__main__':
    unittest.main()
