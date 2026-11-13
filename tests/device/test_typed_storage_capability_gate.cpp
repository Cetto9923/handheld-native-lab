#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

// The Mali patch is gated on a Vulkan capability, not written into the code
// path for every device.
//
// The RG477M (Mali-G615) has no VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT
// for the guest's storage texel buffers, so the engine has to declare a format
// in the module, and the view has to describe the same texels as the declared
// format. That is the only reason the view may take the module's numeric class
// instead of the guest's.
//
// A device that does have the capability (the Honor tablet's Adreno) runs the
// guest's own module with the guest's own view, and this patch must not change
// a single byte of that path. The rule is pure, so the host states both sides:
// with the capability the guest's format is bound unchanged, without it the
// module's class is substituted.
//
// usage: test-typed-storage-capability-gate <module.spv>

static std::vector<uint32_t> load(const char *p){
    std::ifstream in(p,std::ios::binary|std::ios::ate);assert(in.good());
    auto b=in.tellg();assert(b>=20&&b%4==0);in.seekg(0);
    std::vector<uint32_t> w(size_t(b)/4);in.read(reinterpret_cast<char*>(w.data()),b);assert(in.good());
    return w;
}
int main(int argc,char**argv){
    assert(argc>=2);
    auto words=load(argv[1]);
    const XbgStorageBindingInfo info=xbg_spirv_storage_binding_info(words,41);
    assert(info.found);
    assert(info.scalar_class==0);            // the module samples float
    assert(info.declared_format==XBG_IMAGE_FORMAT_UNKNOWN);
    const uint32_t guest=XBG_VK_FORMAT_R32_SINT;   // what the guest binds
    // A device that can run the formatless write keeps the guest's view.
    const XbgTypedStorageViewDecision capable=xbg_typed_storage_view_decision(guest,&info,true);
    assert(capable.formatless_supported);
    assert(!capable.substituted);
    assert(capable.format==guest);
    assert(capable.substituted_format==0);
    // A device that cannot takes the module's class, with the guest's components.
    const XbgTypedStorageViewDecision incapable=xbg_typed_storage_view_decision(guest,&info,false);
    assert(!incapable.formatless_supported);
    assert(incapable.substituted);
    assert(incapable.guest_format==guest);
    assert(incapable.format==XBG_VK_FORMAT_R32_SFLOAT);
    assert(incapable.substituted_format==XBG_VK_FORMAT_R32_SFLOAT);
    assert(xbg_vk_format_components(guest)==xbg_vk_format_components(incapable.format));
    // Without the module's facts there is nothing to align, on any device.
    const XbgTypedStorageViewDecision unknown=xbg_typed_storage_view_decision(guest,nullptr,false);
    assert(!unknown.substituted);
    assert(unknown.format==guest);
    printf("real module d201be268f66c813 binding 41: capability on keeps the guest R32_SINT view, capability off takes the module class PASS\n");
    return 0;
}