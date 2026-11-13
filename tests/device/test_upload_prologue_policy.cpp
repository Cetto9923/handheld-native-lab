#include "../../src/graphics/nvn_vulkan/engine/xbg_upload_policy.h"
#include <cassert>

// The two device rules of the RG477M's Mali are pure functions of the device
// identity, so the host states both sides of each: what the Mali does, and
// what Adreno keeps doing.
//
// usage: test-upload-prologue-policy
int main(){
    // The RG477M's own GPU.
    assert(xbg_device_is_mali(0x13b5u,"Mali-G615 MC6"));
    // Any other Arm Ltd device reports the same vendor id.
    assert(xbg_device_is_mali(0x13b5u,"Mali-G57 MC2"));
    // The name alone identifies a Mali report without the vendor id.
    assert(xbg_device_is_mali(0u,"Mali-G615 MC6"));
    // The tablet's GPU is not a Mali.
    assert(!xbg_device_is_mali(0x5143u,"Adreno (TM) 735"));
    assert(!xbg_device_is_mali(0u,nullptr));

    // The upload placement rule.
    assert(xbg_upload_prologue_default(0x13b5u,"Mali-G615 MC6"));
    assert(!xbg_upload_prologue_default(0x5143u,"Adreno (TM) 735"));
    assert(!xbg_upload_prologue_default(0x5143u,"Adreno (TM) 740"));
    assert(!xbg_upload_prologue_default(0u,"Adreno (TM) 735"));
    assert(!xbg_upload_prologue_default(0x10deu,"NVIDIA GeForce GTX 1080"));
    assert(!xbg_upload_prologue_default(0x8086u,"Intel(R) Iris(TM) Plus Graphics"));
    assert(!xbg_upload_prologue_default(0u,nullptr));

    // The occlusion rule: on the Mali a zero report still counts as visible.
    assert(xbg_occlusion_force_visible_default(0x13b5u,"Mali-G615 MC6"));
    assert(xbg_occlusion_force_visible_default(0u,"Mali-G615 MC6"));
    assert(!xbg_occlusion_force_visible_default(0x5143u,"Adreno (TM) 735"));
    assert(!xbg_occlusion_force_visible_default(0x10deu,"NVIDIA GeForce GTX 1080"));
    assert(!xbg_occlusion_force_visible_default(0u,nullptr));
}
