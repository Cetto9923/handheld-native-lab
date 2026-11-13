#pragma once
#include <cstdint>
#include <cstring>

// Where a whole image upload goes, when the submission has not used that image
// yet.
//
// The RG477M's Mali-G615 is a tiled GPU: ending the open render pass in the
// middle of a frame makes it write the pass's attachments out and read them
// back, so the far scene (sky, moon, ruins) drops out for a frame and the GPU
// pays a round trip. Whole uploads of images the submission has not used yet
// therefore go into the prologue command buffer, ahead of the submission.
//
// Adreno keeps the upload inside the pass: its command structure does not pay
// that round trip, and the measured tablet profile is green with it. The rule
// is a pure function of the device identity, so the host states both sides.
inline bool xbg_upload_prologue_default(uint32_t vendor_id, const char *device_name){
    if(vendor_id==0x13b5u) return true;   // Arm Ltd: Mali
    return device_name && std::strstr(device_name,"Mali")!=nullptr;
}
