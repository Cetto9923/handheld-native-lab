"""Exercise the real touch input method with multiple pointer events."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY = ROOT / 'src/android/app/java/org/ffrs/diagnostic/GameActivity.java'

FIXTURE = r'''
class MotionEvent {
    static final int ACTION_DOWN=0, ACTION_UP=1, ACTION_MOVE=2, ACTION_CANCEL=3,
        ACTION_POINTER_DOWN=5, ACTION_POINTER_UP=6;
    final int action, index;
    final int[] ids;
    final float[] xs;
    MotionEvent(int action, int index, int[] ids, float... xs) {
        this.action=action; this.index=index; this.ids=ids; this.xs=xs;
    }
    int getActionMasked(){return action;}
    int getActionIndex(){return index;}
    int getPointerId(int i){return ids[i];}
    int getPointerCount(){return ids.length;}
    float getX(int i){return xs[i];}
    float getY(int i){return 0;}
}
class View { public boolean onTouchEvent(MotionEvent event){return false;} }
class Log { static void i(String tag,String msg){} }
class Rect { boolean contains(float x,float y){return false;} }
public class TouchR3 extends View {
    static final String TAG="test";
    static final class Ctl { final int bit; final float x; boolean visible=true; Ctl(int bit,float x){this.bit=bit;this.x=x;} }
    final Ctl[] ctls = new Ctl[15];
    final TouchR3 owner=this;
    long touchButtons, pressed, published;
    int touchLx,touchLy,touchRx,touchRy,handleId=-1,lsId=-1,rsId=-1,menuId=-1,r3Id=-1,l3Id=-1,dragMode;
    float lsKx,lsKy,rsKx,rsKy;
    boolean shown=true,menuOpen,editing,handleLong;
    final Object longPress=new Object();
    final Rect menuBtn=new Rect(),handle=new Rect();
    TouchR3() {
        for(int i=0;i<ctls.length;++i) ctls[i]=new Ctl(i,i*10);
        ctls[13]=new Ctl(5,130);
        ctls[14]=new Ctl(4,140);
    }
    void publishPad(){published=touchButtons;}
    void openMenu(){}
    void exitEdit(){}
    void userToggleTouchPad(){}
    void invalidate(){}
    void postDelayed(Object task,int delay){}
    void removeCallbacks(Object task){}
    boolean onMenuTouch(MotionEvent event){return true;}
    boolean onEditTouch(MotionEvent event){return true;}
    float cxOf(Ctl c){return c.x;}
    float cyOf(Ctl c){return 0;}
    float hwOf(Ctl c){return 2;}
    boolean hit(Ctl c,float x,float y){return Math.abs(x-c.x)<=2;}
'''

CHECKS = r'''
    void verifyR3() {
        int[] one={9}, two={9,19};
        onTouchEvent(new MotionEvent(0,0,one,130));
        if (published!=32 || touchRx!=0) throw new AssertionError("down");
        onTouchEvent(new MotionEvent(5,1,two,130,120));
        onTouchEvent(new MotionEvent(2,0,two,136,121));
        if (published!=32 || touchRx==0) throw new AssertionError("hold with right stick");
        onTouchEvent(new MotionEvent(6,0,two,136,121));
        if (published!=0 || touchRx==0) throw new AssertionError("release");
        onTouchEvent(new MotionEvent(5,0,two,130,121));
        onTouchEvent(new MotionEvent(3,0,two,130,121));
        if (published!=0 || touchRx!=0 || r3Id!=-1) throw new AssertionError("cancel");
        onTouchEvent(new MotionEvent(0,0,one,130));
        clearInput();
        if (published!=0 || r3Id!=-1) throw new AssertionError("focus loss");
        onTouchEvent(new MotionEvent(0,0,one,140));
        if (published!=16) throw new AssertionError("L3 down");
        onTouchEvent(new MotionEvent(5,1,two,140,110));
        onTouchEvent(new MotionEvent(2,0,two,140,111));
        if (published!=16 || touchLx==0) throw new AssertionError("L3 with left stick");
        onTouchEvent(new MotionEvent(3,0,two,140,111));
        if (published!=0 || touchLx!=0 || l3Id!=-1) throw new AssertionError("L3 cancel");
        ctls[14].visible=false;
        onTouchEvent(new MotionEvent(0,0,one,140));
        if (published!=0 || l3Id!=-1) throw new AssertionError("hidden L3");
    }
    public static void main(String[] args) { new TouchR3().verifyR3(); }
}
'''


class TouchR3(unittest.TestCase):
    def test_press_hold_and_release(self):
        source = ACTIVITY.read_text()
        touch = source.index('@Override public boolean onTouchEvent(MotionEvent e)')
        body = source[touch:source.index('\n        }', touch)+len('\n        }')]
        clear = source.index('private void clearInput()')
        reset = source[clear:source.index('\n        }', clear)+len('\n        }')]
        self.assertIn('ctls[13] = new Ctl("R3", T_BTN, 5,', source)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'TouchR3.java'
            path.write_text(FIXTURE + body + reset + CHECKS)
            subprocess.run(['javac', str(path)], check=True)
            subprocess.run(['java', '-cp', tmp, 'TouchR3'], check=True)
