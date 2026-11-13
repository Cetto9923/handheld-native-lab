#include "../../src/graphics/nvn_vulkan/engine/xbg_upload_policy.h"
#include <cassert>

// The upload placement rule is a pure function of the device identity, so the
// host states both sides of it: the Mali-G615 of the RG477M takes the whole
// upload ahead of the submission, and Adreno keeps it inside the pass.
//
// usage: test-upload-prologue-policy
int main(){
    // The RG477M's own GPU.
    assert(xbg_upload_prologue_default(0x13b5u,"Mali-G615 MC6"));
    // Any other Arm Ltd device reports the same vendor id.
    assert(xbg_upload_prologue_default(0x13b5u,"Mali-G57 MC2"));
    // The name alone identifies a Mali report without the vendor id.
    assert(xbg_upload_prologue_default(0u,"Mali-G615 MC6"));
    // The tablet's GPU keeps the upload inside the pass.
    assert(!xbg_upload_prologue_default(0x5143u,"Adreno (TM) 735"));
    assert(!xbg_upload_prologue_default(0x5143u,"Adreno (TM) 740"));
    assert(!xbg_upload_prologue_default(0u,"Adreno (TM) 735"));
    // An unknown device keeps the previous behaviour.
    assert(!xbg_upload_prologue_default(0x10deu,"NVIDIA GeForce GTX 1080"));
    assert(!xbg_upload_prologue_default(0x8086u,"Intel(R) Iris(TM) Plus Graphics"));
    assert(!xbg_upload_prologue_default(0u,nullptr));
}
