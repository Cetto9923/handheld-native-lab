#ifndef FFRS_HID_BRIDGE_H
#define FFRS_HID_BRIDGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* SDK Npad state, as in nxnative dm/host/hle_hid.c (MIT). */
typedef struct {
    int64_t sampling;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, reserved;
} ffrs_npad_state;
void ffrs_hid_publish(int connected, uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
uint64_t ffrs_hid_lookup(const char *name, unsigned *arguments, unsigned *returns);
#ifdef __cplusplus
}
#endif
#endif
