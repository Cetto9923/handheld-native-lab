package org.ffrs.diagnostic;

import android.app.Activity;
import android.media.AudioManager;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.os.Handler;
import android.os.Looper;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.RectF;
import android.hardware.input.InputManager;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.ViewGroup;
import android.content.Intent;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.FrameLayout;
import android.widget.ScrollView;

import java.io.File;
import java.lang.ref.WeakReference;

/**
 * Game window form only.
 *
 * The Activity owns a SurfaceView and hands the platform window of that
 * SurfaceView to the native holder. Nothing is rendered into it, no input is
 * consumed and no guest module runs here: this Activity exists so the runtime
 * can obtain a real, presentable window. The measured window geometry is
 * logged next to the geometry the native side reads back from the window, so
 * both can be compared against the existing display snapshot parameters.
 */
public final class GameActivity extends Activity implements SurfaceHolder.Callback, Runnable, DataBundleInstaller.Listener {
    private static final String TAG = "FFRS-GameWindow";
    private SurfaceView surface_view;
    private DisplaySnapshot display_snapshot;
    private boolean resumed;
    private boolean padConnected;
    private long padButtons, axisButtons;
    private int padLx, padLy, padRx, padRy;
    // Virtual (on-screen) pad state; merged with the physical pad in publishPad().
    private long touchButtons;
    private int touchLx, touchLy, touchRx, touchRy;
    private TouchPad touchPad;
    private InputManager inputManager;
    private FrameLayout rootLayout;
    private LinearLayout setupView;
    private TextView setupText;
    private DataBundleInstaller dataInstaller;
    private Button importButton, storageButton, recoverButton, cancelButton;
    private static final int PICK_DATA_BUNDLE = 4701;
    private boolean guestStarted, guestStartFailed, surfaceReady;
    private int guestTerminalState;
    private final Handler guestWatchHandler = new Handler(Looper.getMainLooper());
    private final Runnable guestWatch = new GuestWatch(this);
    private static final GuestStartState GUEST_START = new GuestStartState();
    private static WeakReference<GameActivity> currentActivity = new WeakReference<GameActivity>(null);
    private static WeakReference<GameActivity> surfaceOwner = new WeakReference<GameActivity>(null);
    private DeviceProfile selectedDeviceProfile;
    private AudioManager audioManager;
    private final AudioDeviceCallback audioCallback = new GameAudioDeviceCallback();
    private static final class GameAudioDeviceCallback extends AudioDeviceCallback {
        @Override public void onAudioDevicesAdded(AudioDeviceInfo[] devices) { GameWindowNative.audioDevicesChanged(); }
        @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] devices) { GameWindowNative.audioDevicesChanged(); }
    }

    private void publishPad() {
        boolean touchOn = touchPad != null && touchPad.shown;
        GameWindowNative.setPadState(padConnected || touchOn, padButtons | axisButtons | touchButtons,
                touchLx != 0 ? touchLx : padLx, touchLy != 0 ? touchLy : padLy,
                touchRx != 0 ? touchRx : padRx, touchRy != 0 ? touchRy : padRy);
    }

    private void discoverPad() {
        padConnected = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && ((device.getSources() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (device.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)) {
                padConnected = true;
                Log.i(TAG, "game_input device=" + device.getName() + " id=" + id);
                break;
            }
        }
        updateTouchPadVisibility();
        publishPad();
    }

    /** Marker files/touch-pad: "1" always show the virtual pad, "0" never show it, absent = auto (shown only without a physical pad). */
    private int touchPadMarker() {
        try {
            File dir = getExternalFilesDir(null);
            if (dir == null) return -1;
            File f = new File(dir, "touch-pad");
            if (!f.exists()) return -1;
            java.io.FileInputStream in = new java.io.FileInputStream(f);
            try { int c = in.read(); return c == '1' ? 1 : c == '0' ? 0 : -1; } finally { in.close(); }
        } catch (Exception e) {
            return -1;
        }
    }

    private void updateTouchPadVisibility() {
        if (touchPad == null) return;
        int marker = touchPadMarker();
        boolean show = marker == 1 || (marker < 0 && !padConnected);
        touchPad.setShown(show);
        Log.i(TAG, "game_input touch_pad=" + (show ? "shown" : "hidden") + " marker=" + marker + " physical_pad=" + padConnected);
    }

    /** Called by the on-screen PAD handle: flip the virtual pad and remember the choice in files/touch-pad ('1' / '0'). */
    void userToggleTouchPad() {
        if (touchPad == null) return;
        boolean now = !touchPad.shown;
        touchPad.setShown(now);
        try {
            File dir = getExternalFilesDir(null);
            if (dir != null) {
                java.io.FileOutputStream o = new java.io.FileOutputStream(new File(dir, "touch-pad"));
                o.write(now ? '1' : '0');
                o.close();
            }
        } catch (Exception e) {
            Log.w(TAG, "game_input touch_pad marker write failed: " + e.getMessage());
        }
        publishPad();
        Log.i(TAG, "game_input touch_pad=" + (now ? "shown" : "hidden") + " by_user=1");
    }

    private final PadDeviceListener deviceListener = new PadDeviceListener(this);
    // Static named class: d8 in this build setup rejects anonymous / inner listener classes here.
    private static final class PadDeviceListener implements InputManager.InputDeviceListener {
        private final GameActivity owner;
        PadDeviceListener(GameActivity owner) { this.owner = owner; }
        @Override public void onInputDeviceAdded(int id) { owner.discoverPad(); }
        @Override public void onInputDeviceRemoved(int id) { owner.discoverPad(); }
        @Override public void onInputDeviceChanged(int id) { owner.discoverPad(); }
    }

    /** Map RG477M gamepad events to game button bits, matching physical face-button labels. */
    private static int buttonBit(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_BUTTON_A: return 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1;
            case KeyEvent.KEYCODE_BUTTON_Y: return 3;
            case KeyEvent.KEYCODE_BUTTON_X: return 2;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 4;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 5;
            case KeyEvent.KEYCODE_BUTTON_L1: return 6;
            case KeyEvent.KEYCODE_BUTTON_R1: return 7;
            case KeyEvent.KEYCODE_BUTTON_L2: return 8;
            case KeyEvent.KEYCODE_BUTTON_R2: return 9;
            case KeyEvent.KEYCODE_BUTTON_START: return 10;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 11;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 12;
            case KeyEvent.KEYCODE_DPAD_UP: return 13;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 14;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 15;
            default: return -1;
        }
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (!guestStarted && dataInstaller != null && dataInstaller.managed) return super.dispatchKeyEvent(event);
        int bit = buttonBit(event.getKeyCode());
        if (bit < 0) return super.dispatchKeyEvent(event);
        if (event.getAction() == KeyEvent.ACTION_DOWN) padButtons |= 1L << bit;
        else if (event.getAction() == KeyEvent.ACTION_UP) padButtons &= ~(1L << bit);
        else return super.dispatchKeyEvent(event);
        publishPad();
        Log.i(TAG, "game_input key=" + event.getKeyCode() + " action=" + event.getAction() + " nvn_bit=" + bit);
        return true;
    }

    private static int stick(MotionEvent event, int axis) {
        float value = event.getAxisValue(axis);
        InputDevice device = event.getDevice();
        InputDevice.MotionRange range = device == null ? null : device.getMotionRange(axis, event.getSource());
        if (range != null && Math.abs(value) <= range.getFlat()) value = 0;
        else if (range == null && Math.abs(value) < 0.1f) value = 0;
        return Math.round(Math.max(-1f, Math.min(1f, value)) * 32767f);
    }

    private static boolean hasAxis(InputDevice device, int source, int axis) {
        return device != null && device.getMotionRange(axis, source) != null;
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || event.getAction() != MotionEvent.ACTION_MOVE) return super.onGenericMotionEvent(event);
        InputDevice dev = event.getDevice();
        int src = event.getSource();
        // Pads disagree on which axes carry the right stick and the analog triggers:
        //   Xbox-style (Android default): right stick Z/RZ, triggers LTRIGGER/RTRIGGER (or BRAKE/GAS)
        //   PlayStation-style:            right stick RX/RY, triggers Z/RZ (or BRAKE/GAS)
        // Choose by the axes this device actually reports.
        int rxAxis, ryAxis, ltAxis, rtAxis;
        if (hasAxis(dev, src, MotionEvent.AXIS_RX) && hasAxis(dev, src, MotionEvent.AXIS_RY)) {
            rxAxis = MotionEvent.AXIS_RX; ryAxis = MotionEvent.AXIS_RY;
            if (hasAxis(dev, src, MotionEvent.AXIS_Z) && hasAxis(dev, src, MotionEvent.AXIS_RZ)) { ltAxis = MotionEvent.AXIS_Z; rtAxis = MotionEvent.AXIS_RZ; }
            else if (hasAxis(dev, src, MotionEvent.AXIS_BRAKE) && hasAxis(dev, src, MotionEvent.AXIS_GAS)) { ltAxis = MotionEvent.AXIS_BRAKE; rtAxis = MotionEvent.AXIS_GAS; }
            else { ltAxis = MotionEvent.AXIS_LTRIGGER; rtAxis = MotionEvent.AXIS_RTRIGGER; }
        } else {
            rxAxis = MotionEvent.AXIS_Z; ryAxis = MotionEvent.AXIS_RZ;
            if (hasAxis(dev, src, MotionEvent.AXIS_LTRIGGER) && hasAxis(dev, src, MotionEvent.AXIS_RTRIGGER)) { ltAxis = MotionEvent.AXIS_LTRIGGER; rtAxis = MotionEvent.AXIS_RTRIGGER; }
            else if (hasAxis(dev, src, MotionEvent.AXIS_BRAKE) && hasAxis(dev, src, MotionEvent.AXIS_GAS)) { ltAxis = MotionEvent.AXIS_BRAKE; rtAxis = MotionEvent.AXIS_GAS; }
            else { ltAxis = MotionEvent.AXIS_LTRIGGER; rtAxis = MotionEvent.AXIS_RTRIGGER; }
        }
        padLx = stick(event, MotionEvent.AXIS_X); padLy = -stick(event, MotionEvent.AXIS_Y);
        padRx = stick(event, rxAxis); padRy = -stick(event, ryAxis);
        axisButtons = 0;
        float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X), hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (hatX < -0.5f) axisButtons |= 1L << 12;
        if (hatY < -0.5f) axisButtons |= 1L << 13;
        if (hatX > 0.5f) axisButtons |= 1L << 14;
        if (hatY > 0.5f) axisButtons |= 1L << 15;
        // A key event for the same trigger ORs into the same bit, so reporting both is harmless.
        if (event.getAxisValue(ltAxis) > 0.5f) axisButtons |= 1L << 8;
        if (event.getAxisValue(rtAxis) > 0.5f) axisButtons |= 1L << 9;
        publishPad();
        return true;
    }

    /**
     * On-screen pad drawn over the SurfaceView. Emits the same button bits as buttonBit().
     * Tap the small PAD handle to show/hide; long-press it to edit: drag a control to move it,
     * tap it then use the size slider, drag the opacity slider, Reset / Done. Saved in files/touch-pad.cfg.
     */
    private static final class TouchPad extends View {
        private static final int T_BTN = 0, T_STICK = 1, T_DPAD = 2;
        private static final class Ctl {
            final String name; final int type; final int bit; final float hw, hh;   // half extents at scale 1, in u units
            float fx, fy, scale = 1f;                                                // centre as fraction of the view, size factor
            Ctl(String n, int t, int b, float hw, float hh) { name = n; type = t; bit = b; this.hw = hw; this.hh = hh; }
        }
        private static final class LongPress implements Runnable {
            private final TouchPad pad;
            LongPress(TouchPad p) { pad = p; }
            @Override public void run() { pad.onLongPress(); }
        }

        private final GameActivity owner;
        boolean shown;
        private boolean editing, handleLong;
        private float opacity = 1f;
        private int selected = -1, dragMode;                 // dragMode: 1 opacity, 2 size, 3 move
        private float dragDx, dragDy;
        private final RectF handle = new RectF(), panel = new RectF(), opTrack = new RectF(), scTrack = new RectF();
        private final RectF btnReset = new RectF(), btnDone = new RectF();
        private int handleId = -1, lsId = -1, rsId = -1;
        private float u, vw, vh, lsKx, lsKy, rsKx, rsKy;
        private final Ctl[] ctls = new Ctl[13];
        private long pressed;
        private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Runnable longPress = new LongPress(this);

        TouchPad(GameActivity owner) {
            super(owner);
            this.owner = owner;
            line.setStyle(Paint.Style.STROKE); line.setStrokeWidth(3f);
            fill.setStyle(Paint.Style.FILL);
            text.setTextAlign(Paint.Align.CENTER);
            ctls[0] = new Ctl("A", T_BTN, 0, 0.75f, 0.75f);
            ctls[1] = new Ctl("B", T_BTN, 1, 0.75f, 0.75f);
            ctls[2] = new Ctl("X", T_BTN, 2, 0.75f, 0.75f);
            ctls[3] = new Ctl("Y", T_BTN, 3, 0.75f, 0.75f);
            ctls[4] = new Ctl("L1", T_BTN, 6, 1.6f, 0.6f);
            ctls[5] = new Ctl("L2", T_BTN, 8, 1.6f, 0.6f);
            ctls[6] = new Ctl("R1", T_BTN, 7, 1.6f, 0.6f);
            ctls[7] = new Ctl("R2", T_BTN, 9, 1.6f, 0.6f);
            ctls[8] = new Ctl("-", T_BTN, 11, 1.1f, 0.55f);
            ctls[9] = new Ctl("+", T_BTN, 10, 1.1f, 0.55f);
            ctls[10] = new Ctl("DPAD", T_DPAD, -1, 1.6f, 1.6f);
            ctls[11] = new Ctl("LS", T_STICK, -1, 1.9f, 1.9f);
            ctls[12] = new Ctl("RS", T_STICK, -1, 1.5f, 1.5f);
            setFocusable(false);
        }

        void setShown(boolean s) {
            shown = s;
            if (!s) { editing = false; clearInput(); }
            invalidate();
        }

        private void clearInput() {
            lsId = rsId = -1; pressed = 0; dragMode = 0;
            owner.touchButtons = 0; owner.touchLx = owner.touchLy = owner.touchRx = owner.touchRy = 0;
        }

        private float cxOf(Ctl c) { return c.fx * vw; }
        private float cyOf(Ctl c) { return c.fy * vh; }
        private float hwOf(Ctl c) { return c.hw * u * c.scale; }
        private float hhOf(Ctl c) { return c.hh * u * c.scale; }
        private void place(int i, float px, float py) { ctls[i].fx = px / vw; ctls[i].fy = py / vh; ctls[i].scale = 1f; }

        private void defaults() {
            float w = vw, h = vh;
            float fx = w - 3.1f * u, fy = h - 3.3f * u, d = 1.25f * u;
            place(0, fx, fy + d); place(1, fx + d, fy); place(2, fx - d, fy); place(3, fx, fy - d);
            place(4, 2.0f * u, 2.4f * u); place(5, 2.0f * u, 1.0f * u);
            place(6, w - 2.0f * u, 2.4f * u); place(7, w - 2.0f * u, 1.0f * u);
            place(8, w / 2f - 1.5f * u, h - 0.95f * u); place(9, w / 2f + 1.5f * u, h - 0.95f * u);
            place(10, 2.9f * u, h - 9.4f * u); place(11, 2.9f * u, h - 3.3f * u); place(12, w - 3.1f * u, h - 9.0f * u);
            opacity = 1f;
        }

        @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
            vw = w; vh = h; u = Math.min(w, h) / 15f;
            defaults();
            loadConfig();
            handle.set(w / 2f - 0.8f * u, 0.05f * u, w / 2f + 0.8f * u, 0.6f * u);
            float pw = Math.min(0.46f * w, 18f * u), ph = 7.2f * u;
            panel.set(w / 2f - pw / 2f, h / 2f - ph / 2f, w / 2f + pw / 2f, h / 2f + ph / 2f);
            opTrack.set(panel.left + 4.2f * u, panel.top + 2.6f * u - 0.15f * u, panel.right - 1.0f * u, panel.top + 2.6f * u + 0.15f * u);
            scTrack.set(opTrack.left, panel.top + 4.3f * u - 0.15f * u, opTrack.right, panel.top + 4.3f * u + 0.15f * u);
            btnReset.set(panel.left + 1.0f * u, panel.bottom - 1.7f * u, panel.centerX() - 0.5f * u, panel.bottom - 0.6f * u);
            btnDone.set(panel.centerX() + 0.5f * u, panel.bottom - 1.7f * u, panel.right - 1.0f * u, panel.bottom - 0.6f * u);
        }

        // ---------------- persistence ----------------
        private File cfgFile() { File d = owner.getExternalFilesDir(null); return d == null ? null : new File(d, "touch-pad.cfg"); }

        private void saveConfig() {
            try {
                File f = cfgFile(); if (f == null) return;
                StringBuilder sb = new StringBuilder("opacity ").append(opacity).append('\n');
                for (Ctl c : ctls) sb.append(c.name).append(' ').append(c.fx).append(' ').append(c.fy).append(' ').append(c.scale).append('\n');
                java.io.FileOutputStream o = new java.io.FileOutputStream(f); o.write(sb.toString().getBytes("UTF-8")); o.close();
            } catch (Exception e) { Log.w(TAG, "touch_pad cfg save failed: " + e.getMessage()); }
        }

        private void loadConfig() {
            try {
                File f = cfgFile(); if (f == null || !f.exists()) return;
                java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(new java.io.FileInputStream(f), "UTF-8"));
                String ln;
                while ((ln = r.readLine()) != null) {
                    String[] t = ln.trim().split("\\s+");
                    if (t.length == 2 && t[0].equals("opacity")) { opacity = Math.max(0.15f, Math.min(1f, Float.parseFloat(t[1]))); continue; }
                    if (t.length != 4) continue;
                    for (Ctl c : ctls) if (c.name.equals(t[0])) {
                        c.fx = Math.max(0f, Math.min(1f, Float.parseFloat(t[1])));
                        c.fy = Math.max(0f, Math.min(1f, Float.parseFloat(t[2])));
                        c.scale = Math.max(0.5f, Math.min(2f, Float.parseFloat(t[3])));
                    }
                }
                r.close();
            } catch (Exception e) { Log.w(TAG, "touch_pad cfg load failed: " + e.getMessage()); }
        }

        private void resetDefaults() {
            defaults();
            File f = cfgFile(); if (f != null) f.delete();
            selected = -1; invalidate();
        }

        // ---------------- edit mode ----------------
        void onLongPress() {
            handleLong = true;
            shown = true; clearInput();
            editing = true; selected = -1;
            Log.i(TAG, "game_input touch_pad edit=on");
            invalidate();
        }

        private void exitEdit() {
            editing = false; dragMode = 0; selected = -1;
            saveConfig();
            Log.i(TAG, "game_input touch_pad edit=off opacity=" + opacity);
            invalidate();
        }

        private boolean hit(Ctl c, float x, float y) {
            float cx = cxOf(c), cy = cyOf(c);
            if (c.type == T_BTN) return Math.abs(x - cx) <= hwOf(c) && Math.abs(y - cy) <= hhOf(c);
            return Math.hypot(x - cx, y - cy) <= hwOf(c);
        }

        private float track(RectF t, float x) { return Math.max(0f, Math.min(1f, (x - t.left) / t.width())); }

        private boolean onEditTouch(MotionEvent e) {
            int act = e.getActionMasked(); float x = e.getX(), y = e.getY();
            if (act == MotionEvent.ACTION_DOWN) {
                if (panel.contains(x, y)) {
                    if (btnDone.contains(x, y)) { exitEdit(); return true; }
                    if (btnReset.contains(x, y)) { resetDefaults(); return true; }
                    RectF o = new RectF(opTrack); o.inset(-0.4f * u, -0.6f * u);
                    RectF sc = new RectF(scTrack); sc.inset(-0.4f * u, -0.6f * u);
                    if (o.contains(x, y)) { dragMode = 1; opacity = 0.15f + 0.85f * track(opTrack, x); invalidate(); return true; }
                    if (sc.contains(x, y) && selected >= 0) { dragMode = 2; ctls[selected].scale = 0.5f + 1.5f * track(scTrack, x); invalidate(); return true; }
                    return true;
                }
                for (int i = ctls.length - 1; i >= 0; i--) if (hit(ctls[i], x, y)) {
                    selected = i; dragMode = 3; dragDx = x - cxOf(ctls[i]); dragDy = y - cyOf(ctls[i]); invalidate(); return true;
                }
                selected = -1; invalidate(); return true;
            }
            if (act == MotionEvent.ACTION_MOVE) {
                if (dragMode == 1) opacity = 0.15f + 0.85f * track(opTrack, x);
                else if (dragMode == 2 && selected >= 0) ctls[selected].scale = 0.5f + 1.5f * track(scTrack, x);
                else if (dragMode == 3 && selected >= 0) {
                    Ctl c = ctls[selected];
                    c.fx = Math.max(0f, Math.min(1f, (x - dragDx) / vw)); c.fy = Math.max(0f, Math.min(1f, (y - dragDy) / vh));
                }
                invalidate(); return true;
            }
            if (act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_CANCEL) dragMode = 0;
            return true;
        }

        // ---------------- touch ----------------
        @Override public boolean onTouchEvent(MotionEvent e) {
            int act = e.getActionMasked(), idx = e.getActionIndex();
            if ((act == MotionEvent.ACTION_DOWN || act == MotionEvent.ACTION_POINTER_DOWN)
                    && handleId < 0 && handle.contains(e.getX(idx), e.getY(idx))) {
                handleId = e.getPointerId(idx); handleLong = false;
                postDelayed(longPress, 500);
                invalidate();
                return true;
            }
            if ((act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_POINTER_UP) && handleId >= 0 && e.getPointerId(idx) == handleId) {
                removeCallbacks(longPress);
                boolean inside = handle.contains(e.getX(idx), e.getY(idx));
                handleId = -1;
                if (!handleLong && inside) { if (editing) exitEdit(); else owner.userToggleTouchPad(); }
                invalidate();
                return true;
            }
            if (!shown) return false;
            if (editing) return onEditTouch(e);
            if (act == MotionEvent.ACTION_DOWN || act == MotionEvent.ACTION_POINTER_DOWN) {
                int pid = e.getPointerId(idx); float x = e.getX(idx), y = e.getY(idx);
                Ctl ls = ctls[11], rs = ctls[12];
                if (lsId < 0 && Math.hypot(x - cxOf(ls), y - cyOf(ls)) < hwOf(ls) * 1.6f) lsId = pid;
                else if (rsId < 0 && Math.hypot(x - cxOf(rs), y - cyOf(rs)) < hwOf(rs) * 1.6f) rsId = pid;
            }
            boolean up = act == MotionEvent.ACTION_UP || act == MotionEvent.ACTION_POINTER_UP, cancel = act == MotionEvent.ACTION_CANCEL;
            if (up) { int pid = e.getPointerId(idx); if (pid == lsId) lsId = -1; if (pid == rsId) rsId = -1; }
            long buttons = 0; int lx = 0, ly = 0, rx = 0, ry = 0;
            Ctl ls = ctls[11], rs = ctls[12], dp = ctls[10];
            lsKx = cxOf(ls); lsKy = cyOf(ls); rsKx = cxOf(rs); rsKy = cyOf(rs);
            if (cancel) { lsId = rsId = -1; }
            else {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if (up && i == idx) continue;
                    int pid = e.getPointerId(i); float x = e.getX(i), y = e.getY(i);
                    if (pid == lsId) {
                        float r = hwOf(ls), nx = (x - cxOf(ls)) / r, ny = (y - cyOf(ls)) / r, len = (float) Math.hypot(nx, ny);
                        if (len > 1f) { nx /= len; ny /= len; len = 1f; }
                        if (len > 0.15f) { lx = Math.round(nx * 32767f); ly = Math.round(-ny * 32767f); }
                        lsKx = cxOf(ls) + nx * r; lsKy = cyOf(ls) + ny * r;
                    } else if (pid == rsId) {
                        float r = hwOf(rs), nx = (x - cxOf(rs)) / r, ny = (y - cyOf(rs)) / r, len = (float) Math.hypot(nx, ny);
                        if (len > 1f) { nx /= len; ny /= len; len = 1f; }
                        if (len > 0.15f) { rx = Math.round(nx * 32767f); ry = Math.round(-ny * 32767f); }
                        rsKx = cxOf(rs) + nx * r; rsKy = cyOf(rs) + ny * r;
                    } else {
                        for (int k = 0; k < 10; k++) if (hit(ctls[k], x, y)) buttons |= 1L << ctls[k].bit;
                        float dx = x - cxOf(dp), dy = y - cyOf(dp), dist = (float) Math.hypot(dx, dy), R = hwOf(dp);
                        if (dist < R * 1.1f && dist > R * 0.18f) {
                            if (Math.abs(dx) > Math.abs(dy)) buttons |= dx < 0 ? 1L << 12 : 1L << 14;
                            else buttons |= dy < 0 ? 1L << 13 : 1L << 15;
                        }
                    }
                }
            }
            pressed = buttons;
            if (buttons != owner.touchButtons || lx != owner.touchLx || ly != owner.touchLy || rx != owner.touchRx || ry != owner.touchRy) {
                owner.touchButtons = buttons; owner.touchLx = lx; owner.touchLy = ly; owner.touchRx = rx; owner.touchRy = ry;
                owner.publishPad();
                Log.i(TAG, "game_input touch buttons=0x" + Long.toHexString(buttons) + " L=" + lx + "," + ly + " R=" + rx + "," + ry);
            }
            invalidate();
            return true;
        }

        // ---------------- drawing ----------------
        private int col(int alpha) { return (Math.max(0, Math.min(255, Math.round(alpha * opacity))) << 24) | 0xFFFFFF; }

        @Override protected void onDraw(Canvas c) {
            // Small, low-contrast handle (not affected by the opacity setting so it can always be found).
            fill.setColor(handleId >= 0 ? 0x60FFFFFF : (shown ? 0x20FFFFFF : 0x10FFFFFF));
            line.setColor(0x50FFFFFF); text.setColor(0x80FFFFFF); text.setTextSize(0.4f * u);
            c.drawRoundRect(handle, handle.height() / 3f, handle.height() / 3f, fill);
            c.drawRoundRect(handle, handle.height() / 3f, handle.height() / 3f, line);
            c.drawText(editing ? "OK" : (shown ? "PAD -" : "PAD +"), handle.centerX(), handle.centerY() + text.getTextSize() / 3f, text);
            if (!shown) return;
            text.setTextSize(0.65f * u);
            for (int i = 0; i < ctls.length; i++) drawCtl(c, i);
            if (editing) drawPanel(c);
        }

        private void drawCtl(Canvas c, int i) {
            Ctl k = ctls[i]; float cx = cxOf(k), cy = cyOf(k), hw = hwOf(k), hh = hhOf(k);
            line.setColor(col(0xB0)); text.setColor(col(0xD0)); text.setTextSize(0.65f * u * Math.min(1.4f, k.scale));
            if (k.type == T_BTN) {
                boolean on = (pressed & (1L << k.bit)) != 0;
                RectF r = new RectF(cx - hw, cy - hh, cx + hw, cy + hh);
                fill.setColor(col(on ? 0x70 : 0x28));
                c.drawRoundRect(r, hh / 1.5f, hh / 1.5f, fill); c.drawRoundRect(r, hh / 1.5f, hh / 1.5f, line);
                c.drawText(k.name, cx, cy + text.getTextSize() / 3f, text);
            } else if (k.type == T_STICK) {
                fill.setColor(col(0x20)); c.drawCircle(cx, cy, hw, fill); c.drawCircle(cx, cy, hw, line);
                float kx = i == 11 ? lsKx : rsKx, ky = i == 11 ? lsKy : rsKy;
                if (editing || (i == 11 ? lsId : rsId) < 0) { kx = cx; ky = cy; }
                fill.setColor(col(0x80)); c.drawCircle(kx, ky, hw * 0.4f, fill);
            } else {
                String[] arrow = {"<", "^", ">", "v"}; float[][] off = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}};
                for (int a = 0; a < 4; a++) {
                    boolean on = (pressed & (1L << (12 + a))) != 0;
                    float ax = cx + off[a][0] * hw * 0.6f, ay = cy + off[a][1] * hw * 0.6f, hs = hw * 0.3f;
                    RectF r = new RectF(ax - hs, ay - hs, ax + hs, ay + hs);
                    fill.setColor(col(on ? 0x70 : 0x28));
                    c.drawRoundRect(r, hs / 2f, hs / 2f, fill); c.drawRoundRect(r, hs / 2f, hs / 2f, line);
                    c.drawText(arrow[a], r.centerX(), r.centerY() + text.getTextSize() / 3f, text);
                }
            }
            if (editing && i == selected) {
                Paint.Style st = line.getStyle(); float sw = line.getStrokeWidth();
                line.setColor(0xFFFFD040); line.setStrokeWidth(5f);
                if (k.type == T_BTN) c.drawRect(cx - hw - 6, cy - hh - 6, cx + hw + 6, cy + hh + 6, line);
                else c.drawCircle(cx, cy, hw + 6, line);
                line.setStrokeWidth(sw); line.setStyle(st);
            }
        }

        private void drawPanel(Canvas c) {
            fill.setColor(0xD8101418); c.drawRoundRect(panel, u * 0.5f, u * 0.5f, fill);
            line.setColor(0xA0FFFFFF); c.drawRoundRect(panel, u * 0.5f, u * 0.5f, line);
            text.setColor(0xFFFFFFFF); text.setTextSize(0.5f * u);
            c.drawText("\u62d6\u52a8\u6309\u952e\u79fb\u52a8\u4f4d\u7f6e\uff0c\u70b9\u9009\u540e\u8c03\u5927\u5c0f", panel.centerX(), panel.top + 1.1f * u, text);
            Paint.Align al = text.getTextAlign(); text.setTextAlign(Paint.Align.LEFT);
            c.drawText("\u900f\u660e\u5ea6", panel.left + 1.0f * u, opTrack.centerY() + 0.18f * u, text);
            c.drawText(selected >= 0 ? "\u5927\u5c0f " + ctls[selected].name : "\u5927\u5c0f", panel.left + 1.0f * u, scTrack.centerY() + 0.18f * u, text);
            text.setTextAlign(al);
            drawSlider(c, opTrack, (opacity - 0.15f) / 0.85f, true);
            drawSlider(c, scTrack, selected >= 0 ? (ctls[selected].scale - 0.5f) / 1.5f : 0f, selected >= 0);
            for (int pass = 0; pass < 2; pass++) {
                RectF r = pass == 0 ? btnReset : btnDone;
                fill.setColor(0x40FFFFFF); c.drawRoundRect(r, u * 0.3f, u * 0.3f, fill);
                line.setColor(0xA0FFFFFF); c.drawRoundRect(r, u * 0.3f, u * 0.3f, line);
                text.setColor(0xFFFFFFFF); text.setTextSize(0.6f * u);
                c.drawText(pass == 0 ? "\u91cd\u7f6e" : "\u5b8c\u6210", r.centerX(), r.centerY() + 0.2f * u, text);
            }
        }

        private void drawSlider(Canvas c, RectF t, float v, boolean enabled) {
            v = Math.max(0f, Math.min(1f, v));
            fill.setColor(enabled ? 0x60FFFFFF : 0x20FFFFFF); c.drawRoundRect(t, t.height() / 2f, t.height() / 2f, fill);
            fill.setColor(enabled ? 0xFFFFD040 : 0x40FFFFFF);
            c.drawCircle(t.left + t.width() * v, t.centerY(), 0.45f * u, fill);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        Log.i(TAG, "lifecycle onResume");
        resumed = true;
        discoverPad();
        if (!guestStarted && dataInstaller != null) dataInstaller.rescanDirectory();
        if (!guestStarted) recheckGame();
        GameWindowNative.setFocusState(hasWindowFocus() ? 1 : 2);
    }

    @Override protected void onPause() {
        Log.i(TAG, "lifecycle onPause");
        resumed = false;
        padButtons = axisButtons = 0;
        padLx = padLy = padRx = padRy = 0;
        if (currentActivity.get() == this) {
            publishPad();
            GameWindowNative.setFocusState(3);
        }
        super.onPause();
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        currentActivity = new WeakReference<GameActivity>(this);
        int startState = GUEST_START.get();
        guestStarted = startState == GuestStartState.STARTING || startState == GuestStartState.STARTED;
        guestStartFailed = startState == GuestStartState.FAILED;
        dataInstaller = DataBundleInstaller.get(this);
        dataInstaller.attach(this);
        audioManager = (AudioManager)getSystemService(AUDIO_SERVICE);
        if (audioManager != null) audioManager.registerAudioDeviceCallback(audioCallback, new Handler(Looper.getMainLooper()));
        selectedDeviceProfile = deviceProfile();
        ensureDefaultMarkers();
        configureWindow();
        surface_view = new SurfaceView(this);
        /* The surface format decides whether the compositor reads an alpha
         * channel out of the frame the Vulkan presenter copies into it. The
         * game's final frame carries alpha zero over the whole 3D world and
         * uses alpha only for the HUD pass, so a surface with a real alpha
         * channel makes the compositor blend the world away and only the
         * mini-map survives.
         *
         * The tablet profile asks for an opaque surface: PixelFormat.OPAQUE
         * lets the platform pick a format with no alpha bits, and
         * RGBX_8888 is the explicit opaque-colour variant of RGBA_8888.
         * The community port of this game (d2r-mobile, revision
         * f1e4b081af8ea65d95f0bf490a568cb657c3917e, MIT) does not call
         * setFormat at all and lets the surface keep its default opaque
         * format, and it creates the swapchain with
         * VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR.
         *
         * The handheld keeps RGBA_8888, the format it has always used. */
        if (selectedDeviceProfile != null && selectedDeviceProfile.usesDockedRendering()) {
            surface_view.getHolder().setFormat(PixelFormat.RGBX_8888);
        } else {
            surface_view.getHolder().setFormat(PixelFormat.RGBA_8888);
        }
        FrameLayout root = new FrameLayout(this);
        rootLayout = root;
        root.addView(surface_view, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        touchPad = new TouchPad(this);
        root.addView(touchPad, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        buildSetupView();
        setContentView(root);
        refreshSetup();
        inputManager = (InputManager)getSystemService(Context.INPUT_SERVICE);
        if (inputManager != null) inputManager.registerInputDeviceListener(deviceListener, new Handler(Looper.getMainLooper()));
        surface_view.getHolder().addCallback(this);
        if (startState == GuestStartState.STARTED) startGuestWatch();
    }

    private DeviceProfile deviceProfile() {
        String override = "";
        File dir = getExternalFilesDir(null);
        if (dir != null) {
            try (java.util.Scanner input = new java.util.Scanner(new File(dir, "device-profile"))) {
                if (input.hasNext()) override = input.next();
            } catch (java.io.FileNotFoundException ignored) { }
        }
        String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : Build.HARDWARE;
        int smallestWidthDp = getResources().getConfiguration().smallestScreenWidthDp;
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode mode = display == null ? null : display.getMode();
        int formFactorWidthDp = DeviceProfile.formFactorWidthDp(
                mode == null ? 0 : mode.getPhysicalWidth(),
                mode == null ? 0 : mode.getPhysicalHeight(),
                DisplayMetrics.DENSITY_DEVICE_STABLE, smallestWidthDp);
        DeviceProfile profile = DeviceProfile.select(soc, Build.MODEL, formFactorWidthDp, override);
        Log.i(TAG, "device_profile id=" + profile.id + " soc=" + soc
                + " override=" + (override.isEmpty() ? "none" : override)
                + " smallest_width_dp=" + smallestWidthDp
                + " form_factor_width_dp=" + formFactorWidthDp
                + " target_short_edge=" + profile.targetShortEdge
                + " target_fps=" + profile.targetFps
                + " display_short_edge=" + (mode == null ? 0 : Math.min(mode.getPhysicalWidth(), mode.getPhysicalHeight()))
                + " display_hz=" + (mode == null ? 0 : mode.getRefreshRate())
                + " gpu=see_vulkan_device_log render_resolution=unapplied");
        return profile;
    }

    // Read the same per-package target marker as the native limiter. A changed
    // target takes effect for display selection on the next Activity launch.
    private int requestedGameFps() {
        File dir = getExternalFilesDir(null);
        if (dir != null) {
            try (java.util.Scanner input = new java.util.Scanner(new File(dir, "gfx-target-fps"))) {
                if (input.hasNextInt()) {
                    int value = input.nextInt();
                    if (value >= 0) return value;
                }
            } catch (java.io.FileNotFoundException ignored) {
            }
        }
        return selectedDeviceProfile.targetFps;
    }

    private void ensureDefaultMarkers() {
        File dir = getExternalFilesDir(null);
        if (dir == null) return;
        writeMarkerIfMissing(new File(dir, "gfx-legacy-waitall"), "0\n");
        writeMarkerIfMissing(new File(dir, "gfx-readback-cached"), "1\n");
        writeMarkerIfMissing(new File(dir, "gfx-target-fps"), (selectedDeviceProfile != null ? selectedDeviceProfile.targetFps : 30) + "\n");
        writeMarkerIfMissing(new File(dir, "gfx-depth-aspect"), "1\n");
        writeMarkerIfMissing(new File(dir, "gfx-async-pipelines"), "0\n");
        writeMarkerIfMissing(new File(dir, "gfx-fence-spin-us"), "0\n");
        writeMarkerIfMissing(new File(dir, "gfx-audio-paced-transport"), "1\n");
        writeMarkerIfMissing(new File(dir, "gfx-codex-audio-dump"), "0\n");
        writeMarkerIfMissing(new File(dir, "gfx-codex-frame-metrics"), "1\n");
    }

    private static void writeMarkerIfMissing(File file, String content) {
        if (file.exists()) return;
        try (java.io.FileOutputStream fos = new java.io.FileOutputStream(file)) {
            fos.write(content.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (Throwable ignored) {
        }
    }

    private float requestedDisplayHz = 60.0f;

    /** Full screen window so the SurfaceView covers the whole display. */
    private void configureWindow() {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (surface_view != null) surface_view.setKeepScreenOn(true);
        try {
            int cur = android.provider.Settings.System.getInt(getContentResolver(),
                    android.provider.Settings.System.SCREEN_BRIGHTNESS, 128);
            WindowManager.LayoutParams lp = window.getAttributes();
            lp.screenBrightness = Math.max(0.05f, Math.min(1.0f, cur / 255.0f));
            int target = requestedGameFps();
            // A rate request is a request, not a hint. Asking this surface for
            // 60 Hz on a 120 Hz panel was measured at 51.8 ms per frame
            // (19.3 present/s): the 33.3 ms frame budget lands on the 16.7 ms
            // grid and the producer misses the buffer release. Asking for the
            // panel rate explicitly (120 Hz) measured the same 19.3 present/s,
            // while the same build without any rate request held 33.3 ms per
            // frame (30.00 present/s). So for a 30 FPS target the window takes
            // the panel rate as it is; only the higher targets, which need a
            // rate of their own, ask for one.
            requestedDisplayHz = 0.0f;
            Display display = getWindowManager().getDefaultDisplay();
            if ((target == 0 || target > 30) && display != null) {
                Display.Mode current = display.getMode();
                boolean compatibleOnly = false;
                File dataDir = getExternalFilesDir(null);
                if (selectedDeviceProfile.usesDockedRendering() && target > 0 && dataDir != null) {
                    try (java.io.FileInputStream marker = new java.io.FileInputStream(new File(dataDir, "gfx-display-compatible"))) {
                        compatibleOnly = marker.read() == '1';
                    } catch (java.io.IOException ignored) { }
                }
                if (compatibleOnly) {
                    boolean found = false;
                    for (Display.Mode mode : display.getSupportedModes()) {
                        float multiple = mode.getRefreshRate() / target;
                        if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                                && mode.getPhysicalHeight() == current.getPhysicalHeight()
                                && multiple >= 1.0f && Math.abs(multiple - Math.round(multiple)) < 0.01f) found = true;
                    }
                    compatibleOnly = found;
                }
                for (Display.Mode mode : display.getSupportedModes()) {
                    float multiple = target > 0 ? mode.getRefreshRate() / target : 0;
                    if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                            && mode.getPhysicalHeight() == current.getPhysicalHeight()
                            && (!compatibleOnly || (multiple >= 1.0f && Math.abs(multiple - Math.round(multiple)) < 0.01f))
                            && mode.getRefreshRate() > requestedDisplayHz) {
                        requestedDisplayHz = mode.getRefreshRate();
                        lp.preferredDisplayModeId = mode.getModeId();
                    }
                }
                lp.preferredRefreshRate = requestedDisplayHz;
            }
            Log.i(TAG, "display_request: target_fps=" + target
                    + " requested_hz=" + requestedDisplayHz
                    + " mode_id=" + lp.preferredDisplayModeId);
            window.setAttributes(lp);
        } catch (Throwable ignored) {
        }
        // Touch the decor view first: the window insets controller only exists
        // once the decor view is installed and attached.
        View decor = window.getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
        } else {
            window.setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                    WindowManager.LayoutParams.FLAG_FULLSCREEN);
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        GameWindowNative.setFocusState(resumed ? (hasFocus ? 1 : 2) : 3);
        if (!hasFocus) return;
        if (!guestStarted) recheckGame();
        Window window = getWindow();
        View decor = window.getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller == null) controller = window.getInsetsController();
            if (controller == null) {
                Log.w(TAG, "window insets controller is not available yet");
                return;
            }
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        } else {
            decor.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            Log.e(TAG, "surface_created without a valid Surface; no window is published");
            return;
        }
        if (Build.VERSION.SDK_INT >= 30 && requestedDisplayHz > 0.0f) {
            try {
                surface.setFrameRate(requestedDisplayHz, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
                Log.i(TAG, "surface_frame_rate_request: hz=" + requestedDisplayHz);
            } catch (IllegalArgumentException e) {
                Log.w(TAG, "surface_frame_rate_request rejected", e);
            }
        }
        display_snapshot = captureDisplaySnapshot();
        long[] window = GameWindowNative.setSurface(surface);
        if (window == null) {
            Log.e(TAG, "surface_created: native window call returned null");
            return;
        }
        surfaceOwner = new WeakReference<GameActivity>(this);
        DisplaySnapshot snapshot = display_snapshot;
        Log.i(TAG, "window_form"
                + " display_width_px=" + snapshot.width_px
                + " display_height_px=" + snapshot.height_px
                + " content_width_px=" + snapshot.content_width_px
                + " content_height_px=" + snapshot.content_height_px
                + " surface_width_px=" + surface_view.getWidth()
                + " surface_height_px=" + surface_view.getHeight()
                + " inset_left_px=" + snapshot.inset_left_px
                + " inset_top_px=" + snapshot.inset_top_px
                + " inset_right_px=" + snapshot.inset_right_px
                + " inset_bottom_px=" + snapshot.inset_bottom_px
                + " size_source=" + snapshot.size_source
                + " insets_source=" + snapshot.insets_source
                + " density_dpi=" + snapshot.density_dpi
                + " refresh_rate_hz=" + snapshot.refresh_rate_hz
                + " rotation=" + snapshot.rotation
                + " native_present=" + window[0]
                + " native_pointer=0x" + Long.toHexString(window[1])
                + " native_width_px=" + window[2]
                + " native_height_px=" + window[3]
                + " native_format=" + window[4]
                + " native_generation=" + window[5]
                + " error=" + snapshot.error);
        // The Vulkan presentation probe runs off the UI thread: the driver
        // calls must not happen on the thread that owns the SurfaceView.
        // It is opt-in, because it paints the screen with the probe's own
        // colour and would hide whatever the guest presents.
        if (getIntent() != null && getIntent().getBooleanExtra("run_probe", false)) {
            Thread probe = new Thread(new VulkanProbeTask(), "ffrs-vk-probe");
            probe.start();
        }
        // The guest runs in this process, because the window is a
        // process-local platform handle. It starts after the window is
        // published, so the display bridge finds a real window.
        surfaceReady = true;
        if (getIntent() == null || getIntent().getBooleanExtra("run_guest", true)) {
            if (!guestStarted) startGuest();
        }
    }

    /** Legacy location, only writable through adb; kept so existing adb installs keep working. */
    private static final String DEFAULT_GAME_DIR =
            "/data/local/tmp/ffrs-60fps";
    private static final String RG477M_GAME_DIR =
            "/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259";

    private static final String[] REQUIRED_PAKS = {
            "FFRS-Switch.pak",
            "FFRS-Switch.ucas",
            "FFRS-Switch.utoc",
            "global.ucas",
            "global.utoc"
    };

    static java.util.List<String> findMissingPaks(File gameDir) {
        java.util.ArrayList<String> missing = new java.util.ArrayList<String>();
        if (gameDir == null) return missing;
        File paksDir = new File(gameDir, "romfs/FFRS/Content/Paks");
        for (String name : REQUIRED_PAKS) {
            File f = new File(paksDir, name);
            if (!f.isFile() || f.length() == 0) {
                missing.add(name);
            }
        }
        return missing;
    }

    private static boolean hasGame(File dir) {
        if (dir == null) return false;
        File nso = new File(dir, "main.nso");
        File rom = new File(dir, "romfs");
        if (!nso.canRead() || (!rom.isDirectory() && !rom.canRead())) return false;
        return findMissingPaks(dir).isEmpty();
    }

    /** Bounded shallow scan searching for directories named "FFRS" (case-insensitive) under roots. */
    private static void shallowScanForFfrs(File dir, int depth, int maxDepth, java.util.List<File> results, int[] counter, int maxEntries) {
        if (dir == null || depth > maxDepth || counter[0] >= maxEntries) return;
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            if (counter[0] >= maxEntries) break;
            if (child == null || !child.isDirectory()) continue;
            counter[0]++;
            String name = child.getName();
            if (name.startsWith(".") || name.equalsIgnoreCase("Android") || name.equalsIgnoreCase("DCIM")
                    || name.equalsIgnoreCase("Pictures") || name.equalsIgnoreCase("Movies")
                    || name.equalsIgnoreCase("Music") || name.equalsIgnoreCase("Alarms")
                    || name.equalsIgnoreCase("Podcasts") || name.equalsIgnoreCase("Ringtones")
                    || name.equalsIgnoreCase("Notifications") || name.equalsIgnoreCase("Recordings")
                    || name.equalsIgnoreCase("MIUI") || name.equalsIgnoreCase("ColorOS")
                    || name.equalsIgnoreCase("Huawei") || name.equalsIgnoreCase("Honor")) {
                continue;
            }
            if (name.equalsIgnoreCase("FFRS")) {
                if (!results.contains(child)) results.add(child);
            } else if (depth < maxDepth) {
                shallowScanForFfrs(child, depth + 1, maxDepth, results, counter, maxEntries);
            }
        }
    }

    /** Candidate game folders, best first. The user puts the whole FFRS folder (main.nso + romfs) on the storage. */
    private File[] candidateGameDirs() {
        java.util.LinkedHashSet<File> list = new java.util.LinkedHashSet<File>();
        // Public roots come from Android, not a device model or a fixed SD UUID.
        java.util.LinkedHashSet<File> roots = new java.util.LinkedHashSet<File>();
        roots.add(Environment.getExternalStorageDirectory());
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.os.storage.StorageManager manager =
                    (android.os.storage.StorageManager) getSystemService(STORAGE_SERVICE);
            if (manager != null) {
                for (android.os.storage.StorageVolume volume : manager.getStorageVolumes()) {
                    File root = volume.getDirectory();
                    if (root != null && (Environment.MEDIA_MOUNTED.equals(volume.getState())
                            || Environment.MEDIA_MOUNTED_READ_ONLY.equals(volume.getState()))) {
                        roots.add(root);
                    }
                }
            }
        }
        // Also discover storage volume roots across all Android versions via getExternalFilesDirs.
        for (File ext : getExternalFilesDirs(null)) {
            if (ext != null) {
                String path = ext.getAbsolutePath();
                int idx = path.indexOf("/Android/data");
                if (idx > 0) {
                    File r = new File(path.substring(0, idx));
                    if (r.isDirectory() && r.canRead()) roots.add(r);
                }
            }
        }
        // Direct examination of mounted storage locations (e.g. SD cards, USB OTG).
        File storageRoot = new File("/storage");
        if (storageRoot.isDirectory()) {
            File[] subs = storageRoot.listFiles();
            if (subs != null) {
                for (File s : subs) {
                    if (s != null && s.isDirectory() && s.canRead()) {
                        String name = s.getName();
                        if (!name.equals("self") && !name.equals("knox")) {
                            roots.add(s);
                        }
                    }
                }
            }
        }
        File mntMediaRw = new File("/mnt/media_rw");
        if (mntMediaRw.isDirectory()) {
            File[] subs = mntMediaRw.listFiles();
            if (subs != null) {
                for (File s : subs) {
                    if (s != null && s.isDirectory() && s.canRead()) {
                        roots.add(s);
                    }
                }
            }
        }

        // Bounded common paths: standard directory structures on internal/external volumes.
        String[] common = {
            "FFRS", "ffrs",
            "HandheldNativeLab/data", "HandheldNativeLab",
            "Games/FFRS", "games/FFRS", "Games/ffrs", "games/ffrs",
            "Download/FFRS", "download/FFRS", "Download/ffrs", "download/ffrs",
            "Documents/FFRS", "documents/FFRS", "Documents/ffrs", "documents/ffrs",
            "ROMs/FFRS", "roms/FFRS", "ROMs/ffrs", "roms/ffrs",
            "Switch/FFRS", "switch/FFRS", "Switch/ffrs", "switch/ffrs",
            "ROMs/Switch/FFRS", "roms/switch/FFRS"
        };
        for (File root : roots) {
            for (String child : common) list.add(new File(root, child));
            list.add(root); // Data copied directly to a removable volume root.
        }

        // App-specific external storage: works out-of-the-box without MANAGE_EXTERNAL_STORAGE permission.
        for (File ext : getExternalFilesDirs(null)) {
            if (ext != null) {
                list.add(new File(ext, "game"));
                list.add(new File(ext, "FFRS"));
                list.add(new File(ext, "ffrs"));
                list.add(ext);
            }
        }

        // Bounded shallow scan for FFRS directories located 1-2 levels down from roots.
        java.util.ArrayList<File> shallowFound = new java.util.ArrayList<File>();
        int[] counter = new int[]{0};
        for (File root : roots) {
            shallowScanForFfrs(root, 1, 2, shallowFound, counter, 64);
        }
        for (File f : shallowFound) list.add(f);

        // Retain development and baseline RG477M candidate directories.
        list.add(new File(RG477M_GAME_DIR));
        list.add(new File(DEFAULT_GAME_DIR));
        File tmp = new File("/data/local/tmp");
        if (tmp.isDirectory()) {
            File[] subs = tmp.listFiles();
            if (subs != null) {
                for (File s : subs) {
                    if (s != null && s.isDirectory() && s.getName().startsWith("ffrs-") && !list.contains(s)) {
                        list.add(s);
                    }
                }
            }
        }
        return list.toArray(new File[0]);
    }

    private String resolveGameDir() {
        String fromIntent = getIntent() == null ? null : getIntent().getStringExtra("game_dir");
        if (fromIntent != null && !fromIntent.isEmpty()) {
            Log.i(TAG, "resolveGameDir: intent override game_dir=" + fromIntent);
            return fromIntent;
        }
        if (dataInstaller != null && dataInstaller.managed) {
            String dir = dataInstaller.root == null ? null : dataInstaller.root.getAbsolutePath();
            if (dir != null) {
                Log.i(TAG, "resolveGameDir: managed installer root=" + dir);
                return dir;
            }
        }
        for (File d : candidateGameDirs()) {
            if (hasGame(d)) {
                String resolved = d.getAbsolutePath();
                Log.i(TAG, "resolveGameDir: autodiscovered game_dir=" + resolved);
                return resolved;
            }
        }
        Log.w(TAG, "resolveGameDir: no valid game directory found in candidates");
        return null;
    }

    private boolean allFilesAccess() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager();
    }

    private void buildSetupView() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        box.setBackgroundColor(0xFF101418);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        box.setPadding(pad, pad, pad, pad);
        TextView t = new TextView(this);
        t.setTextColor(0xFFFFFFFF);
        t.setTextSize(18f);
        t.setGravity(Gravity.CENTER);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(t);
        box.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        Button grant = new Button(this);
        grant.setText("\u6388\u4e88\u6587\u4ef6\u8bbf\u95ee\u6743\u9650");
        grant.setOnClickListener(new SetupClick(this, 0));
        Button retry = new Button(this);
        retry.setText("\u91cd\u65b0\u68c0\u67e5\u6e38\u620f\u6570\u636e");
        retry.setOnClickListener(new SetupClick(this, 1));
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        bp.topMargin = pad / 2;
        if (dataInstaller != null && dataInstaller.managed) {
            if (dataInstaller.directoryMode) {
                importButton = setupButton("允许读取游戏目录", 0, box);
                recoverButton = setupButton("重新查找并检查目录", 6, box);
            } else {
                importButton = setupButton("选择同目录的数据包", 2, box);
                storageButton = setupButton("切换导入位置", 3, box);
                recoverButton = setupButton("验证并恢复已导入版本", 4, box);
            }
            cancelButton = setupButton(dataInstaller.directoryMode ? "取消检查" : "取消导入", 5, box);
        } else {
            box.addView(grant, bp);
            box.addView(retry, bp);
        }
        box.setVisibility(View.GONE);
        box.setClickable(true);
        setupView = box;
        setupText = t;
        rootLayout.addView(box, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private Button setupButton(String label, int action, LinearLayout box) {
        Button button = new Button(this);
        button.setText(label);
        button.setOnClickListener(new SetupClick(this, action));
        box.addView(button, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return button;
    }

    @Override public void onDataStateChanged() {
        if (isFinishing() || isDestroyed() || guestStarted) return;
        refreshSetup();
        if (!dataInstaller.busy && dataInstaller.root != null) recheckGame();
    }

    private void chooseDataBundle() {
        if (dataInstaller.busy) return;
        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(pick, PICK_DATA_BUNDLE); }
        catch (android.content.ActivityNotFoundException e) {
            setupText.setText("此设备没有系统文件选择器，请先启用系统文件管理器。数据未改动。");
        }
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_DATA_BUNDLE && result == RESULT_OK && data != null && data.getData() != null)
            dataInstaller.install(data.getData());
    }

    /** Named listener (not anonymous/inner): d8 in this build setup rejects those. */
    private static final class SetupClick implements View.OnClickListener {
        private final GameActivity owner; private final int action;
        SetupClick(GameActivity o, int a) { owner = o; action = a; }
        @Override public void onClick(View v) {
            if (action == 0) owner.requestFileAccess();
            else if (action == 1) owner.recheckGame();
            else if (action == 2) owner.chooseDataBundle();
            else if (action == 3) owner.dataInstaller.cycleStorage();
            else if (action == 4) owner.dataInstaller.recover();
            else if (action == 5) owner.dataInstaller.cancel();
            else if (action == 6) owner.dataInstaller.rescanDirectory();
        }
    }

    void requestFileAccess() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName()));
                startActivity(i);
            } else {
                requestPermissions(new String[] { "android.permission.READ_EXTERNAL_STORAGE" }, 1);
            }
        } catch (Exception e) {
            try { startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)); }
            catch (Exception e2) { Log.w(TAG, "file access settings unavailable: " + e2.getMessage()); }
        }
    }

    void recheckGame() {
        if (guestStarted) return;
        startGuest();
    }

    private void refreshSetup() {
        if (setupView == null) return;
        if (guestStarted) { setupView.setVisibility(View.GONE); return; }
        if (dataInstaller != null && dataInstaller.managed) {
            setupText.setText("Handheld Native Lab · Preview\n\n" + dataInstaller.message
                    + "\n\n" + dataInstaller.storageText());
            importButton.setEnabled(!dataInstaller.busy);
            if (storageButton != null) storageButton.setEnabled(!dataInstaller.busy);
            recoverButton.setEnabled(!dataInstaller.busy);
            cancelButton.setVisibility(dataInstaller.busy ? View.VISIBLE : View.GONE);
            setupView.setVisibility(View.VISIBLE);
            return;
        }
        File ext = getExternalFilesDir(null);
        StringBuilder sb = new StringBuilder();
        sb.append("【最终幻想：共鸣 · 数据安装与自动发现】\n\n");

        File[] candidates = candidateGameDirs();
        java.util.ArrayList<File> partialDirs = new java.util.ArrayList<File>();
        for (File d : candidates) {
            if (d == null) continue;
            boolean nso = new File(d, "main.nso").canRead();
            boolean rom = new File(d, "romfs").isDirectory();
            if (nso || rom) partialDirs.add(d);
        }

        if (!partialDirs.isEmpty()) {
            for (File d : partialDirs) {
                boolean nso = new File(d, "main.nso").canRead();
                boolean rom = new File(d, "romfs").isDirectory();
                java.util.List<String> missing = findMissingPaks(d);
                if (nso && rom && !missing.isEmpty()) {
                    sb.append("⚠️ 【数据包不完整】发现目录：").append(d.getAbsolutePath()).append("\n");
                    sb.append("缺少以下关键 Pak 文件（位于 romfs/FFRS/Content/Paks/）：\n");
                    for (String name : missing) {
                        sb.append("   ❌ ").append(name).append("\n");
                    }
                    sb.append("请将缺失文件放入对应目录后，点击下方「重新检查游戏数据」。\n\n");
                } else if (!nso || !rom) {
                    sb.append("⚠️ 【发现部分文件】").append(d.getAbsolutePath())
                            .append("\n  main.nso: ").append(nso ? "✓ 正常" : "❌ 未找到")
                            .append("\n  romfs: ").append(rom ? "✓ 正常" : "❌ 未找到")
                            .append("\n请确保 FFRS 文件夹内同时包含 main.nso 与完整的 romfs/ 目录。\n\n");
                }
            }
        } else {
            sb.append("未在设备常见路径中检测到完整游戏数据。\n\n");
        }

        if (!allFilesAccess()) {
            sb.append("⚠️ 【尚未授予所有文件访问权限】\n");
            sb.append("Android 11+ 系统限制访问共享存储。\n");
            sb.append("若将游戏放在手机内置存储或外置存储卡，请点击下方「授予文件访问权限」按钮，在系统设置中允许本应用管理所有文件。\n\n");
        } else {
            sb.append("✓ 已获取「所有文件访问」权限。\n\n");
        }

        sb.append("【支持的数据放置路径】（拷入其一即可自动识别）：\n");
        sb.append("1. 内部存储根目录：\n   ").append(Environment.getExternalStorageDirectory().getAbsolutePath()).append("/FFRS\n");
        sb.append("2. 常用游戏目录：\n   ").append(Environment.getExternalStorageDirectory().getAbsolutePath()).append("/Games/FFRS\n");
        sb.append("3. 外置存储卡 / USB OTG：\n   <外置存储根目录>/FFRS 或 <外置存储根目录>/Games/FFRS\n");
        if (ext != null) {
            sb.append("4. 应用专属目录（免权限）：\n   ").append(ext.getAbsolutePath()).append("/game\n");
        }

        sb.append("\n检查时间：").append(new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.US).format(new java.util.Date())).append("\n");
        sb.append("已检查候选路径：\n");
        int count = 0;
        for (File d : candidates) {
            if (d == null) continue;
            boolean nso = new File(d, "main.nso").canRead(), rom = new File(d, "romfs").isDirectory();
            sb.append(" • ").append(d.getAbsolutePath()).append(" [nso:").append(nso ? "✓" : "✗").append(" rom:").append(rom ? "✓" : "✗");
            if (nso && rom) {
                java.util.List<String> missing = findMissingPaks(d);
                if (!missing.isEmpty()) sb.append(" 缺Pak:").append(missing.size()).append("个");
                else sb.append(" 完整✓");
            }
            sb.append("]\n");
            if (++count >= 8 && partialDirs.isEmpty()) {
                sb.append(" • ... 其余 ").append(candidates.length - count).append(" 个候选路径\n");
                break;
            }
        }
        setupText.setText(sb.toString());
        setupView.setVisibility(View.VISIBLE);
    }
    /** Explicit Android port resource policy; logs/queries report actual owned budgets. */
    // Real startup exceeded the former 1 GiB diagnostic ceiling. RG477M
    // reports 12 GiB RAM with >8 GiB available; reserve a bounded 3 GiB
    // logical guest backing budget, reported truthfully through QueryMemoryInfo.
    private static final long GUEST_QUOTA_BYTES = 3221225472L;
    private static final long GUEST_STACK_BYTES = 2097152L;
    // Runtime page/ticket metadata: 16 MiB exhausted during real startup.
    // This is a bounded host bookkeeping pool, separate from guest backing quota.
    private static final long GUEST_RESOURCE_BYTES = 67108864L;

    /** Starts the owned guest on its own thread inside this app process. */
    private void startGuest() {
        if (dataInstaller != null && dataInstaller.managed && dataInstaller.busy) { refreshSetup(); return; }
        if (guestStarted || (getIntent() != null
                && !getIntent().getBooleanExtra("run_guest", true))) return;
        // Surface callbacks may arrive after onPause. A Surface alone does
        // not mean an interactive Activity owns a presentable window yet.
        android.os.PowerManager power =
                (android.os.PowerManager)getSystemService(POWER_SERVICE);
        Surface surface = surface_view == null ? null : surface_view.getHolder().getSurface();
        boolean interactive = power != null && power.isInteractive();
        boolean valid = surfaceReady && surface != null && surface.isValid()
                && surface_view.getWidth() > 0 && surface_view.getHeight() > 0;
        if (!resumed || !hasWindowFocus() || !interactive || !valid) {
            Log.i(TAG, "guest_start deferred resumed=" + resumed + " focused=" + hasWindowFocus()
                    + " interactive=" + interactive + " surfaceReady=" + surfaceReady
                    + " validSurface=" + valid);
            return;
        }
        String game_dir = resolveGameDir();
        if (game_dir == null) {
            Log.e(TAG, "guest_image_unreadable: no game folder found; showing setup screen");
            refreshSetup();
            return;
        }
        java.util.List<String> missingPaks = findMissingPaks(new File(game_dir));
        if (!missingPaks.isEmpty()) {
            Log.e(TAG, "guest_paks_missing in " + game_dir + ": " + missingPaks);
            refreshSetup();
            return;
        }
        String image = game_dir + "/main.nso";
        String romfs = game_dir + "/romfs";
        if (!new java.io.File(image).canRead()) {
            Log.e(TAG, "guest_image_unreadable path=" + image);
            refreshSetup();
            return;
        }
        if (!GUEST_START.begin()) {
            int state = GUEST_START.get();
            guestStarted = state == GuestStartState.STARTING || state == GuestStartState.STARTED;
            guestStartFailed = state == GuestStartState.FAILED;
            refreshSetup();
            return;
        }
        guestStarted = true;
        boolean managed = dataInstaller != null && dataInstaller.managed && dataInstaller.root != null
                && dataInstaller.root.getAbsolutePath().equals(game_dir);
        File files = managed ? dataInstaller.runtimeRoot : getExternalFilesDir(null);
        String saveRoot = managed ? new File(getFilesDir(), "saves").getAbsolutePath() : null;
        boolean isolatedSave = getIntent() != null
                && getIntent().getBooleanExtra("isolated_regression_save", false);
        try {
            saveRoot = RegressionSaveRoot.select(saveRoot, isolatedSave,
                    (getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0,
                    getFilesDir(), getIntent() == null ? null
                            : getIntent().getStringExtra("isolated_regression_resume"));
        } catch (java.io.IOException | SecurityException e) {
            Log.e(TAG, "isolated regression save unavailable; guest start aborted", e);
            return;
        }
        if (isolatedSave) Log.i(TAG, "isolated regression save root=" + saveRoot);
        guestStarted = true;
        Log.i(TAG, "guest_start hide_setup_view");
        if (setupView != null) setupView.setVisibility(View.GONE);
        Log.i(TAG, "guest_start setup_view_hidden");
        Log.i(TAG, "game_dir=" + game_dir);
        String op_mode = getIntent() == null ? null : getIntent().getStringExtra("operation_mode");
        if (op_mode != null && !op_mode.isEmpty() && files != null) {
            try {
                java.io.FileOutputStream fos = new java.io.FileOutputStream(new File(files, "oe-operation-mode"));
                fos.write(op_mode.getBytes());
                fos.close();
                Log.i(TAG, "operation_mode configured from intent: " + op_mode);
            } catch (Throwable t) {
                Log.w(TAG, "failed to persist operation_mode: " + t.getMessage());
            }
        }
        GameWindowNative.setProfileTargetFps(selectedDeviceProfile.targetFps,
                selectedDeviceProfile.usesDockedRendering());
        String log_path = files == null ? null : new File(files, "guest_boot.log").getAbsolutePath();
        Thread guest = new Thread(new GuestBootTask(image, romfs, log_path, saveRoot), "ffrs-guest");
        guest.start();
    }

    /** A named task, so the dexer sees a plain class and not an anonymous one. */
    private static final class GuestBootTask implements Runnable {
        private final String image;
        private final String romfs;
        private final String log_path, saveRoot;
        GuestBootTask(String image, String romfs, String log_path, String saveRoot) {
            this.image = image;
            this.romfs = romfs;
            this.log_path = log_path;
            this.saveRoot = saveRoot;
        }
        @Override public void run() {
            long thread = -1;
            try {
                thread = saveRoot == null ? GameWindowNative.nativeStartGuest(image, romfs, log_path,
                        GUEST_QUOTA_BYTES, GUEST_STACK_BYTES, GUEST_RESOURCE_BYTES, new String[0])
                        : GameWindowNative.nativeStartInstalledGuest(image, romfs, log_path,
                        GUEST_QUOTA_BYTES, GUEST_STACK_BYTES, GUEST_RESOURCE_BYTES, new String[0], saveRoot);
            } catch (RuntimeException | LinkageError | OutOfMemoryError error) {
                Log.e(TAG, "guest_start native call failed", error);
            }
            Log.i(TAG, "guest_start thread=" + thread + " guest_in_app_process=true"
                    + " transcript=" + log_path);
            GUEST_START.finish(thread != -1);
            new Handler(Looper.getMainLooper()).post(new GuestStartChanged());
        }
    }

    /** Resolve the current Activity when the worker finishes, not the one it started with. */
    private static final class GuestStartChanged implements Runnable {
        @Override public void run() {
            GameActivity activity = currentActivity.get();
            if (activity == null || activity.isFinishing() || activity.isDestroyed()) return;
            int state = GUEST_START.get();
            activity.guestStarted = state == GuestStartState.STARTING || state == GuestStartState.STARTED;
            activity.guestStartFailed = state == GuestStartState.FAILED;
            activity.refreshSetup();
            if (state == GuestStartState.STARTED) activity.startGuestWatch();
        }
    }

    private void startGuestWatch() {
        guestWatchHandler.removeCallbacks(guestWatch);
        guestWatchHandler.post(guestWatch);
    }

    private static final class GuestWatch implements Runnable {
        private final WeakReference<GameActivity> activity;
        GuestWatch(GameActivity owner) { activity = new WeakReference<GameActivity>(owner); }
        @Override public void run() {
            GameActivity owner = activity.get();
            if (owner == null || currentActivity.get() != owner || owner.isFinishing() || owner.isDestroyed()) return;
            int state = GameWindowNative.nativeGuestThreadState();
            if (state == 2 || state == 3) {
                owner.guestTerminalState = state;
                owner.refreshSetup();
            } else if (owner.guestStarted) owner.guestWatchHandler.postDelayed(this, 500);
        }
    }

    /** A named task, so the dexer sees a plain class and not an anonymous one. */
    private static final class VulkanProbeTask implements Runnable {
        @Override public void run() {
            int rc = GameWindowNative.nativeRunVulkanProbe();
            Log.i(TAG, "vulkan_probe rc=" + rc + " (host probe, not a game frame)");
        }
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (currentActivity.get() != this) return;
        Log.i(TAG, "surface_changed format=" + format
                + " width_px=" + width + " height_px=" + height);
        long[] window = GameWindowNative.describeWindow();
        if (window != null) {
            Log.i(TAG, "window_readback present=" + window[0]
                    + " pointer=0x" + Long.toHexString(window[1])
                    + " width_px=" + window[2] + " height_px=" + window[3]
                    + " format=" + window[4] + " generation=" + window[5]);
        }
        if (!guestStarted) startGuest();
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surface_destroyed entering");
        surfaceReady = false;
        if (surfaceOwner.get() == this) {
            GameWindowNative.clearSurface();
            surfaceOwner.clear();
            Log.i(TAG, "surface_destroyed native_window_cleared");
        }
    }

    private final Handler exitHandler = new Handler(Looper.getMainLooper());
    @Override public void run() {
        if (isFinishing() || isDestroyed()) return;
        if (GameWindowNative.hostExitReady(false)) finish();
        else exitHandler.postDelayed(this, 50);
    }
    @Override public void onBackPressed() {
        if (!guestStarted) {
            if (dataInstaller != null && dataInstaller.busy) dataInstaller.cancel();
            else finish();
            return;
        }
        if (GameWindowNative.hostExitReady(true)) finish();
        else { exitHandler.removeCallbacks(this); exitHandler.postDelayed(this, 50); }
    }

    @Override protected void onStop() {
        Log.i(TAG, "lifecycle onStop");
        super.onStop();
    }

    @Override protected void onDestroy() {
        Log.i(TAG, "lifecycle onDestroy");
        exitHandler.removeCallbacks(this);
        guestWatchHandler.removeCallbacks(guestWatch);
        if (dataInstaller != null) dataInstaller.detach(this);
        if (audioManager != null) audioManager.unregisterAudioDeviceCallback(audioCallback);
        if (surfaceOwner.get() == this) {
            GameWindowNative.clearSurface();
            surfaceOwner.clear();
        }
        if (currentActivity.get() == this) currentActivity.clear();
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    private DisplaySnapshot captureDisplaySnapshot() {
        // Measured on the UI thread with the same parameters the diagnostic
        // carrier already uses, so the two can be compared directly.
        DisplaySnapshot snapshot = new DisplaySnapshot();
        snapshot.captured_elapsed_realtime_ns = SystemClock.elapsedRealtimeNanos();
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        snapshot.density_dpi = metrics.densityDpi;
        snapshot.density = metrics.density;
        View content = findViewById(android.R.id.content);
        if (content != null) {
            snapshot.content_width_px = content.getWidth();
            snapshot.content_height_px = content.getHeight();
        }
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                android.view.WindowMetrics window = getWindowManager().getCurrentWindowMetrics();
                Rect bounds = window.getBounds();
                snapshot.width_px = bounds.width();
                snapshot.height_px = bounds.height();
                snapshot.size_source = "WindowManager.currentWindowMetrics.bounds";
                Insets insets = window.getWindowInsets().getInsetsIgnoringVisibility(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                snapshot.inset_left_px = insets.left;
                snapshot.inset_top_px = insets.top;
                snapshot.inset_right_px = insets.right;
                snapshot.inset_bottom_px = insets.bottom;
                snapshot.insets_source = DisplaySnapshot.MODERN_INSETS_SOURCE;
                snapshot.insets_measured = true;
            } else {
                View decor = getWindow().getDecorView();
                snapshot.width_px = decor.getWidth();
                snapshot.height_px = decor.getHeight();
                snapshot.size_source = "Window.decorView.measured_bounds";
            }
        } catch (RuntimeException failure) {
            snapshot.error = "Window measurement: " + failure.toString();
        }
        try {
            Display display = Build.VERSION.SDK_INT >= 30
                    ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (display != null) {
                snapshot.display_id = display.getDisplayId();
                snapshot.rotation = display.getRotation();
                snapshot.refresh_rate_hz = display.getRefreshRate();
                snapshot.current_mode_id = display.getMode().getModeId();
            }
        } catch (RuntimeException failure) {
            snapshot.error += " Display measurement: " + failure.toString();
        }
        return snapshot;
    }
}
