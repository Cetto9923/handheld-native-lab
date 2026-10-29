/* One actual Android controller, published by the Activity. No simulated
 * presses, motion sensor, vibration motor, mouse, or keyboard is advertised. */
#include "hid_bridge.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(ffrs_npad_state) == 0x28, "Npad ABI");
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ffrs_npad_state current;
static uint64_t pressed;
static uint32_t styles = UINT32_MAX, ids = 0x101, hold;
static int connected, initialized;
static int32_t clamp(int32_t value) { return value < -32767 ? -32767 : value > 32767 ? 32767 : value; }
void ffrs_hid_publish(int on, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    pthread_mutex_lock(&lock);
    connected = !!on;
    buttons = on ? buttons & 0xffff : 0;
    pressed |= buttons & ~current.buttons;
    current.buttons = buttons;
    current.lx = on ? clamp(lx) : 0; current.ly = on ? clamp(ly) : 0;
    current.rx = on ? clamp(rx) : 0; current.ry = on ? clamp(ry) : 0;
    if (!on) pressed = 0;
    pthread_mutex_unlock(&lock);
}
static void initialize(void) {
    pthread_mutex_lock(&lock); initialized = 1; pthread_mutex_unlock(&lock);
    fprintf(stderr, "[hle] hid initialized: input comes from Android events\n");
}
static void set_styles(uint32_t value) { pthread_mutex_lock(&lock); styles = value; pthread_mutex_unlock(&lock); }
static void set_ids(const uint32_t *values, uint64_t count) {
    uint32_t mask = 0;
    for (uint64_t i = 0; values && i < count && i < 10; i++) {
        if (values[i] < 8) mask |= 1u << values[i];
        else if (values[i] == 0x20) mask |= 1u << 8;
    }
    pthread_mutex_lock(&lock); ids = mask; pthread_mutex_unlock(&lock);
}
static uint32_t style_locked(uint32_t id) {
    if (!initialized || !connected) return 0;
    if (id == 0 && (ids & 1) && (styles & 1)) return 1;
    if (id == 0x20 && !(styles & 1) && (ids & 0x100) && (styles & 2)) return 2;
    return 0;
}
static uint32_t get_style(const uint32_t *id) {
    pthread_mutex_lock(&lock); uint32_t s = id ? style_locked(*id) : 0; pthread_mutex_unlock(&lock); return s;
}
static void set_hold(uint32_t value) { pthread_mutex_lock(&lock); hold = value; pthread_mutex_unlock(&lock); }
static uint32_t get_hold(void) { pthread_mutex_lock(&lock); uint32_t value = hold; pthread_mutex_unlock(&lock); return value; }
static void state(ffrs_npad_state *out, const uint32_t *id, uint32_t style) {
    if (!out) return;
    pthread_mutex_lock(&lock);
    memset(out, 0, sizeof(*out));
    out->sampling = ++current.sampling;
    if (id && (style_locked(*id) & style)) {
        int64_t sequence = out->sampling; *out = current; out->sampling = sequence;
        out->buttons |= pressed; pressed = 0;
        const int32_t axes[4] = {out->lx, out->ly, out->rx, out->ry};
        if (axes[0] < -0x4000) out->buttons |= 1u << 16;
        if (axes[1] > 0x4000) out->buttons |= 1u << 17;
        if (axes[0] > 0x4000) out->buttons |= 1u << 18;
        if (axes[1] < -0x4000) out->buttons |= 1u << 19;
        if (axes[2] < -0x4000) out->buttons |= 1u << 20;
        if (axes[3] > 0x4000) out->buttons |= 1u << 21;
        if (axes[2] > 0x4000) out->buttons |= 1u << 22;
        if (axes[3] < -0x4000) out->buttons |= 1u << 23;
        out->attributes = style == 2 ? 0x3f : 0x17;
    }
    pthread_mutex_unlock(&lock);
}
static void full(ffrs_npad_state *s, const uint32_t *id) { state(s, id, 1); }
static void handheld(ffrs_npad_state *s, const uint32_t *id) { state(s, id, 2); }
static void dual(ffrs_npad_state *s, const uint32_t *id) { state(s, id, 4); }
static void left(ffrs_npad_state *s, const uint32_t *id) { state(s, id, 8); }
static void right(ffrs_npad_state *s, const uint32_t *id) { state(s, id, 16); }
static void keyboard(void *out) { if (out) memset(out, 0, 0x30); }
static void mouse(void *out) { if (out) memset(out, 0, 0x28); }
static int no_handles(uint32_t *out, int count, const uint32_t *id, uint32_t style) {
    (void)out; (void)count; (void)id; (void)style; return 0;
}
static int no_sensors(void *out, int count, const uint32_t *handle) {
    (void)out; (void)count; (void)handle; return 0;
}
static void unsupported_device(const uint32_t *handle) {
    (void)handle; fprintf(stderr, "[hle] hid sensor/vibration handle has no Android backend\n");
}
static void vibration_info(uint32_t *out, const uint32_t *handle) {
    (void)handle; if (out) { out[0] = 0; out[1] = 0; }
}
static void vibration_send(const uint32_t *handle, const void *value) { (void)value; unsupported_device(handle); }
static void gyro_mode(const uint32_t *handle, const int32_t *mode) { (void)mode; unsupported_device(handle); }

typedef struct { const char *name; void (*fn)(void); unsigned arguments, returns; } entry;
#define B(name,fn,args,rets) {name,(void (*)(void))fn,args,rets}
static const entry entries[] = {
 B("_ZN2nn3hid14InitializeNpadEv",initialize,0,0),
 B("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",set_styles,1,0),
 B("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm",set_ids,2,0),
 B("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE",set_hold,1,0),
 B("_ZN2nn3hid18GetNpadJoyHoldTypeEv",get_hold,0,1),
 B("_ZN2nn3hid15GetNpadStyleSetERKj",get_style,1,1),
 B("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj",full,2,0),
 B("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj",handheld,2,0),
 B("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj",dual,2,0),
 B("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj",left,2,0),
 B("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj",right,2,0),
 B("_ZN2nn3hid16GetKeyboardStateEPNS0_13KeyboardStateE",keyboard,1,0),
 B("_ZN2nn3hid13GetMouseStateEPNS0_10MouseStateE",mouse,1,0),
 B("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",no_handles,4,1),
 B("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE",unsupported_device,1,0),
 B("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE",vibration_info,2,0),
 B("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE",vibration_send,2,0),
 B("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",no_handles,4,1),
 B("_ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE",no_sensors,3,1),
 B("_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE",unsupported_device,1,0),
 B("_ZN2nn3hid17StopSixAxisSensorERKNS0_19SixAxisSensorHandleE",unsupported_device,1,0),
 B("_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE",gyro_mode,2,0),
};
uint64_t ffrs_hid_lookup(const char *name, unsigned *arguments, unsigned *returns) {
    if (!name) return 0;
    for (size_t i = 0; i < sizeof(entries)/sizeof(*entries); i++) if (!strcmp(name, entries[i].name)) {
        if (arguments) *arguments = entries[i].arguments;
        if (returns) *returns = entries[i].returns;
        return (uint64_t)(uintptr_t)entries[i].fn;
    }
    return 0;
}
