#pragma once
#include <cstdint>
#include <cstring>

// The RG477M's Mali-G615 needs two device rules that the measured Adreno
// profile does not. Both are pure functions of the device identity, so the
// host states both sides.

// The Mali of the RG477M, whatever it reports as its name.
inline bool xbg_device_is_mali(uint32_t vendor_id, const char *device_name){
    if(vendor_id==0x13b5u) return true;   // Arm Ltd: Mali
    return device_name && std::strstr(device_name,"Mali")!=nullptr;
}

// Where a whole image upload goes, when the submission has not used that image
// yet.
//
// The RG477M's Mali-G615 is a tiled GPU: ending the open render pass in the
// middle of a frame makes it write the pass's attachments out and read them
// back. Whole uploads of images the submission has not used yet therefore go
// into the prologue command buffer, ahead of the submission.
//
// Adreno keeps the upload inside the pass: its command structure does not pay
// that round trip, and the measured tablet profile is green with it.
inline bool xbg_upload_prologue_default(uint32_t vendor_id, const char *device_name){
    return xbg_device_is_mali(vendor_id,device_name);
}

// Whether a samples-passed report of zero still counts as visible.
//
// The game culls a block of the scene when the occlusion query reports no
// samples. The engine's report value is the sum of the query results that are
// known by the time the report is written; a result that the GPU has not
// returned yet reads as zero, and "not returned yet" is indistinguishable from
// "really occluded" in that sum. On the Mali of the RG477M the far scene of
// the map and of the battle scene then drops out as a whole block, and comes
// back when the query lands a frame later.
//
// Adreno keeps the report as it is: its query results arrive in time, and the
// culling is what makes the scene cheap there.
inline bool xbg_occlusion_force_visible_default(uint32_t vendor_id, const char *device_name){
    return xbg_device_is_mali(vendor_id,device_name);
}
