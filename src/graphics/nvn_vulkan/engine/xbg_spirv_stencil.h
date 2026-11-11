#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// The exact FFRS histogram kernel scans workgroup memory. On Adreno 735 its
// memory-only barrier omitted 64 entries in measured frames; workgroup execution
// barriers before and after the shared load preserve all lanes. Match the
// original bundle's precise instruction IDs and constants, so an unfamiliar
// shader or user override is left untouched rather than patched by guesswork.
inline bool xbg_ffrs_histogram_workgroup_sync(std::vector<uint32_t> &w){
    if(w.size()<5 || w[0]!=0x07230203u)return false;
    size_t mem=size_t(-1),load=size_t(-1);bool scope=false,sem=false,oldsem=false;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(!n || i+n>w.size())return false;
        if(op==43 && n==4 && w[i+1]==1){if(w[i+2]==2179 && w[i+3]==2)scope=true;if(w[i+2]==2180 && w[i+3]==264)sem=true;if(w[i+2]==4618 && w[i+3]==3400)oldsem=true;}
        if(op==225 && n==3 && w[i+1]==2179 && w[i+2]==4618){if(mem!=size_t(-1))return false;mem=i;}
        if(op==61 && n==4 && w[i+1]==1 && w[i+2]==4629 && w[i+3]==4628){if(load!=size_t(-1))return false;load=i;}
        i+=n;
    }
    if(!scope || !sem || !oldsem || mem==size_t(-1) || load==size_t(-1) || load<=mem || load-mem>80)return false;
    std::vector<uint32_t> out;out.reserve(w.size()+5);
    out.insert(out.end(),w.begin(),w.begin()+mem);
    const uint32_t barrier[]={4u<<16|224u,2179,2179,2180};out.insert(out.end(),barrier,barrier+4);
    out.insert(out.end(),w.begin()+mem+3,w.begin()+load+4);out.insert(out.end(),barrier,barrier+4);
    out.insert(out.end(),w.begin()+load+4,w.end());w.swap(out);return true;
}

// FFRS texel-buffer numeric facts observed on the real device. These kernels
// use raw Maxwell register bits, but the translator's sampler declarations
// default to FP32. Only the observed bindings are specialized; float/half
// bindings in the same modules retain their original declarations.
struct XbgFfrsIntegerFact { const char *key; uint32_t binding,scalar; bool storage; };
inline constexpr XbgFfrsIntegerFact xbg_ffrs_integer_facts[]={
    {"876dd0820a7ad8fa",29,2,false},
    {"876dd0820a7ad8fa",53,2,true},
    {"664748e31a2dfad9",11,1,true},
    {"b673e10f3da2451c",17,1,false},
    {"b673e10f3da2451c",35,2,false},
    {"b673e10f3da2451c",41,1,true},
    {"b673e10f3da2451c",47,2,true},
    // Honor frame5752 simulation: native signed attribute input/output views.
    // This closes their type mismatch; it does not fix the compaction sum.
    {"cb997f17e1c89c8b",29,2,false},
    {"cb997f17e1c89c8b",47,2,true},
    {"5b0df93f0a696a7f",11,1,true},
    {"ef4326df31036d24",11,1,false},
    {"ef4326df31036d24",17,1,true},
    {"527f4a4f540bfc19",11,1,false},
    {"527f4a4f540bfc19",17,1,true},
    {"70a97cd190a8738f",11,1,false},
    {"70a97cd190a8738f",17,1,false},
    {"70a97cd190a8738f",23,1,false},
    {"70a97cd190a8738f",29,1,true},
    {"70a97cd190a8738f",35,1,true},
    {"9bee7f09d6c655c3",11,1,false},
    {"9bee7f09d6c655c3",17,1,false},
    {"9bee7f09d6c655c3",29,1,true},
    {"9bee7f09d6c655c3",35,1,true},
    {"3b6302a58b86a34a",47,1,true},
    {"da4ffe0a328303aa",17,1,true},
    // Particle vertex shaders (stage 0): Niagara raw uint instance index buffers
    // Group A: raw instance/lookup words at B12/B24. Original-run captures
    // 8691/23325 identify B18 as native R16_SFLOAT/range2 for 26ae,
    // 8b27 and 9528: leave that numeric half fetch float, like the pairs below.
    {"26aef111ba3b3c4e",12,1,false},
    {"26aef111ba3b3c4e",24,1,false},
    {"8b27994fdc0e32c9",12,1,false},
    {"8b27994fdc0e32c9",24,1,false},
    // These spell/LIMIT slot129 descriptors are half-float attributes, not raw
    // instance words. Keep binding18 float so its native R16_SFLOAT/range2
    // view converts half values correctly (including nonzero and negative zero).
    {"41a5a80e5242a26c",12,1,false},
    {"41a5a80e5242a26c",24,1,false},
    {"db5f1e2dd1c9ef3d",12,1,false},
    {"db5f1e2dd1c9ef3d",24,1,false},
    // LIMIT break vertex shaders (stage 0): Niagara raw uint instance index buffers
    {"632c207422d773c5",12,1,false},
    {"632c207422d773c5",24,1,false},
    {"085afbc955a0744f",12,1,false},
    {"085afbc955a0744f",24,1,false},
    // LIMIT ice shards: frame37722/draw3495377 binds R32_UINT at B12.
    // Preserve raw instance-index bits; B18 remains native R16_SFLOAT.
    {"d1d6ceeb3a7b2c18",12,1,false},
    // LIMIT trails: the same frame's draw3495303 has native R32_UINT B12.
    {"db559389b69f3033",12,1,false},
    // Iron Giant landing debris: frame5752/color475432 raw instance indices.
    {"ae837eccb9eff189",12,1,false},
    // LIMIT fire outro, frame10983: all three pre/color pairs use native
    // R32_UINT instance indices at B12. B18 stays R16_SFLOAT/range2.
    {"8fad1c673a152dcf",12,1,false},
    {"85e1ac91e6c257c9",12,1,false},
    {"8771a4b045b64a8d",12,1,false},
    {"55703bda012e6143",12,1,false},
    {"c10d897690572731",12,1,false},
    {"92bc83108242870a",12,1,false},
    {"9528f530e1c95ce5",12,1,false},
    {"9528f530e1c95ce5",24,1,false},
    // Group B: the five observed particle shaders have raw B18/B30 but numeric
    // R16_SFLOAT/range2 B24 (same captures). Do not extend this fact to fe033,
    // whose B24 was not observed; retain its separate existing mapping below.
    {"be4e89a0687ae37c",18,1,false},
    {"be4e89a0687ae37c",30,1,false},
    {"431a6183c3565f66",18,1,false},
    {"431a6183c3565f66",30,1,false},
    {"1e222af40b26bd25",18,1,false},
    {"1e222af40b26bd25",30,1,false},
    {"b0284572b5f180f2",18,1,false},
    {"b0284572b5f180f2",30,1,false},
    {"6018ff2b44b763d7",18,1,false},
    {"6018ff2b44b763d7",30,1,false},
    {"fe033d948255d9ff",18,1,false},
    {"fe033d948255d9ff",24,1,false},
    {"fe033d948255d9ff",30,1,false}
};
inline uint32_t xbg_ffrs_integer_fact(uint32_t stage,const char *key,uint32_t binding){
    if((stage==5 || stage==0) && key)for(const auto &f:xbg_ffrs_integer_facts)
        if(binding==f.binding && !std::strcmp(key,f.key))return f.scalar;
    return 0;
}

// These integer image bindings were observed on the device, and their fetches
// immediately recover Maxwell register bits with float-to-int bitcasts.
inline uint32_t xbg_ffrs_uint_binding(uint32_t stage,const char *key){
    if(!key)return ~0u;
    if(stage==5){
        if(!std::strcmp(key,"ee333831da910601"))return 17;
        if(!std::strcmp(key,"c1e92bc373efae07"))return 11;
    }
    if(stage!=4)return ~0u;
    if(!std::strcmp(key,"1b69b9b28f994df3"))return 10;
    if(!std::strcmp(key,"78e88a70347e22dc"))return 46;
    if(!std::strcmp(key,"062df44b75d37d76"))return 58;
    if(!std::strcmp(key,"9b0acd9875f7c44a"))return 22;
    return ~0u;
}
inline uint32_t xbg_ffrs_uint_store_binding(uint32_t stage,const char *key){
    if(stage!=5 || !key)return ~0u;
    if(!std::strcmp(key,"ee333831da910601"))return 23;
    if(!std::strcmp(key,"c1e92bc373efae07"))return 17;
    return ~0u;
}

struct XbgIntegerImageTypes {
    uint32_t image=0,sampled=0,pointer=0;
    std::vector<uint32_t> declarations;
    std::unordered_set<size_t> moved;
};
// Non-aggregate SPIR-V types must be unique. Reuse an existing integer image
// type chain, moving a later declaration before the newly typed variable when
// necessary. This preserves IDs and every other descriptor's declaration.
inline XbgIntegerImageTypes xbg_integer_image_types(const std::vector<uint32_t> &w,size_t image_pos,size_t pointer_pos,size_t variable_pos,uint32_t scalar,bool sampled,uint32_t &bound){
    XbgIntegerImageTypes r;size_t image_at=size_t(-1),sampled_at=size_t(-1),pointer_at=size_t(-1),scalar_at=size_t(-1);
    uint32_t image_n=w[image_pos]>>16;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==21 && w[i+1]==scalar)scalar_at=i;
        if(op==25 && n==image_n && w[i+2]==scalar){bool equal=true;for(uint32_t j=3;j<n;j++)equal=equal && w[i+j]==w[image_pos+j];if(equal){r.image=w[i+1];image_at=i;}}
        i+=n;
    }
    if(!r.image)r.image=bound++;
    if(sampled){
        for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(op==27 && n==3 && w[i+2]==r.image){r.sampled=w[i+1];sampled_at=i;}i+=n;}
        if(!r.sampled)r.sampled=bound++;
    }
    uint32_t pointee=sampled?r.sampled:r.image;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(op==32 && n==4 && w[i+2]==w[pointer_pos+2] && w[i+3]==pointee){r.pointer=w[i+1];pointer_at=i;}i+=n;}
    if(!r.pointer)r.pointer=bound++;
    auto move=[&](size_t at){if(at!=size_t(-1) && at>variable_pos){r.declarations.insert(r.declarations.end(),w.begin()+at,w.begin()+at+(w[at]>>16));r.moved.insert(at);}};
    move(scalar_at);
    if(image_at==size_t(-1)){size_t at=r.declarations.size();r.declarations.insert(r.declarations.end(),w.begin()+image_pos,w.begin()+image_pos+image_n);r.declarations[at+1]=r.image;r.declarations[at+2]=scalar;}else move(image_at);
    if(sampled){if(sampled_at==size_t(-1))r.declarations.insert(r.declarations.end(),{(3u<<16)|27u,r.sampled,r.image});else move(sampled_at);}
    if(pointer_at==size_t(-1))r.declarations.insert(r.declarations.end(),{(4u<<16)|32u,r.pointer,w[pointer_pos+2],pointee});else move(pointer_at);
    return r;
}

// Clone only the selected descriptor's type chain. Other float textures in the
// same module must keep their original sampled type and arithmetic.
inline int xbg_spirv_integer_fetch(std::vector<uint32_t> &w,uint32_t binding,uint32_t scalar){
    if(scalar!=1 && scalar!=2)return -1;
    if(w.size()<5 || w[0]!=0x07230203u)return -1;
    std::unordered_map<uint32_t,size_t> def;uint32_t variable=0,uint32=0,float4=0,uint4=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(!n || i+n>w.size())return -1;
        if(op>=19 && op<=33 && n>=2)def[w[i+1]]=i;
        if(op==59 && n>=4)def[w[i+2]]=i;
        if(op==71 && n==4 && w[i+2]==33 && w[i+3]==binding)variable=w[i+1];
        if(op==21 && n==4 && w[i+2]==32 && w[i+3]==uint32_t(scalar==2))uint32=w[i+1];i+=n;
    }
    if(!variable || !uint32 || !def.count(variable))return -1;
    size_t vi=def[variable];if((w[vi]&65535u)!=59 || !def.count(w[vi+1]))return -1;
    size_t pi=def[w[vi+1]];if((w[pi]&65535u)!=32 || !def.count(w[pi+3]))return -1;
    size_t si=def[w[pi+3]];if((w[si]&65535u)!=27 || !def.count(w[si+2]))return -1;
    size_t ii=def[w[si+2]];if((w[ii]&65535u)!=25 || !def.count(w[ii+2]))return -1;
    size_t fi=def[w[ii+2]];if((w[fi]&65535u)!=22 || w[fi+2]!=32)return -1;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==23 && n==4 && w[i+3]==4){if(w[i+2]==w[ii+2])float4=w[i+1];if(w[i+2]==uint32)uint4=w[i+1];}i+=n;
    }
    if(!float4)return -1;
    uint32_t bound=w[3];auto types=xbg_integer_image_types(w,ii,pi,vi,uint32,true,bound);uint32_t image=types.image,sampled=types.sampled,pointer=types.pointer;bool add_vector=!uint4;if(add_vector)uint4=bound++;
    std::unordered_set<uint32_t> loads,images;int fetches=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==61 && n>=4 && w[i+3]==variable)loads.insert(w[i+2]);
        if(op==100 && n==4 && loads.count(w[i+3]))images.insert(w[i+2]);i+=n;
    }
    std::vector<uint32_t> out(w.begin(),w.begin()+5);
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(types.moved.count(i)){i+=n;continue;}
        if(i==vi){
            out.insert(out.end(),types.declarations.begin(),types.declarations.end());
            if(add_vector)out.insert(out.end(),{(4u<<16)|23u,uint4,uint32,4u});
        }
        size_t at=out.size();out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==vi)out[at+1]=pointer;
        if(op==61 && loads.count(w[i+2]))out[at+1]=sampled;
        if(op==100 && images.count(w[i+2]))out[at+1]=image;
        if(op==95 && n>=5 && images.count(w[i+3])){
            if(w[i+1]!=float4)return -1;uint32_t raw=bound++;out[at+1]=uint4;out[at+2]=raw;
            out.insert(out.end(),{(4u<<16)|124u,float4,w[i+2],raw});++fetches;
        }
        i+=n;
    }
    if(fetches<1)return -1;out[3]=bound;w.swap(out);return fetches;
}
inline int xbg_spirv_uint_fetch(std::vector<uint32_t> &w,uint32_t binding){
    return xbg_spirv_integer_fetch(w,binding,1);
}

// Storage counterparts of the two proven volumetric-lightmap UINT fetches.
// Keep the original coordinate/value calculations; only carry their raw bits
// through a Vulkan-conformant uvec4 image write, avoiding float conversion.
inline int xbg_spirv_integer_storage(std::vector<uint32_t> &w,uint32_t binding,uint32_t scalar){
    if(scalar!=1 && scalar!=2)return -1;
    if(w.size()<5 || w[0]!=0x07230203u)return -1;
    std::unordered_map<uint32_t,size_t> def;uint32_t variable=0,uint32=0,float4=0,uint4=0;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(!n || i+n>w.size())return -1;
        if(op>=19 && op<=33 && n>=2)def[w[i+1]]=i;
        if(op==59 && n>=4)def[w[i+2]]=i;
        if(op==71 && n==4 && w[i+2]==33 && w[i+3]==binding)variable=w[i+1];
        if(op==21 && n==4 && w[i+2]==32 && w[i+3]==uint32_t(scalar==2))uint32=w[i+1];i+=n;
    }
    if(!variable || !uint32 || !def.count(variable))return -1;
    size_t vi=def[variable];if((w[vi]&65535u)!=59 || !def.count(w[vi+1]))return -1;
    size_t pi=def[w[vi+1]];if((w[pi]&65535u)!=32 || !def.count(w[pi+3]))return -1;
    size_t ii=def[w[pi+3]];if((w[ii]&65535u)!=25 || w[ii+7]!=2 || !def.count(w[ii+2]))return -1;
    size_t fi=def[w[ii+2]];if((w[fi]&65535u)!=22 || w[fi+2]!=32)return -1;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==23 && n==4 && w[i+3]==4){if(w[i+2]==w[ii+2])float4=w[i+1];if(w[i+2]==uint32)uint4=w[i+1];}i+=n;
    }
    if(!float4)return -1;
    uint32_t bound=w[3];auto types=xbg_integer_image_types(w,ii,pi,vi,uint32,false,bound);uint32_t image=types.image,pointer=types.pointer;bool add_vector=!uint4;if(add_vector)uint4=bound++;
    std::unordered_set<uint32_t> loads;
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;if(op==61 && n>=4 && w[i+3]==variable)loads.insert(w[i+2]);i+=n;}
    if(loads.empty())return -1;int stores=0,reads=0;std::vector<uint32_t> out(w.begin(),w.begin()+5);
    for(size_t i=5;i<w.size();){uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(types.moved.count(i)){i+=n;continue;}
        if(i==vi){out.insert(out.end(),types.declarations.begin(),types.declarations.end());if(add_vector)out.insert(out.end(),{(4u<<16)|23u,uint4,uint32,4u});}
        uint32_t raw=0;if(op==99 && n>=4 && loads.count(w[i+1])){raw=bound++;out.insert(out.end(),{(4u<<16)|124u,uint4,raw,w[i+3]});++stores;}
        size_t at=out.size();out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==vi)out[at+1]=pointer;if(op==61 && loads.count(w[i+2]))out[at+1]=image;if(raw)out[at+3]=raw;
        if(op==98 && n>=5 && loads.count(w[i+3])){
            if(w[i+1]!=float4)return -1;uint32_t fetched=bound++;out[at+1]=uint4;out[at+2]=fetched;
            out.insert(out.end(),{(4u<<16)|124u,float4,w[i+2],fetched});++reads;
        }
        i+=n;
    }
    if(!stores && !reads)return -1;out[3]=bound;w.swap(out);return stores+reads;
}
inline int xbg_spirv_uint_store(std::vector<uint32_t> &w,uint32_t binding){
    return xbg_spirv_integer_storage(w,binding,1);
}

// Clustered lighting texel fetch repair (Earth Shrine exterior draw 131709, shader 97ae64dbc5e7ff26).
// Binding 64 is a texel buffer view (VK_FORMAT_R16_UINT) holding light index offsets and indices.
// The translator originally fetched float then bitcast to int via a float Function variable.
// Storing small integers (1, 2, ...) as float bits creates subnormal (denormal) IEEE floats,
// which mobile GPU ALU registers in FTZ (Flush-To-Zero) mode flush to 0.0f, corrupting
// every light index to 0 and creating square block anomalies on the ground.
// This function retargets the image to OpTypeImage %uint Buffer, fetches uint4, extracts
// uint, stores to uint variables, and loads as uint before the existing OpBitcast %int.
inline int xbg_spirv_shrine_lighting_fetch(std::vector<uint32_t> &w,uint32_t binding){
    if(w.size()<5 || w[0]!=0x07230203u)return -1;
    std::unordered_map<uint32_t,size_t> def;
    uint32_t variable=0,uint32=0,float4=0,uint4=0,ptr_func_uint=0;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(!n || i+n>w.size())return -1;
        if(op>=19 && op<=33 && n>=2)def[w[i+1]]=i;
        if(op==59 && n>=4)def[w[i+2]]=i;
        if(op==71 && n==4 && w[i+2]==33 && w[i+3]==binding)variable=w[i+1];
        if(op==21 && n==4 && w[i+2]==32 && w[i+3]==0)uint32=w[i+1];
        if(op==32 && n==4 && w[i+2]==7 && uint32 && w[i+3]==uint32)ptr_func_uint=w[i+1];
        i+=n;
    }
    if(!variable || !uint32 || !def.count(variable))return -1;
    if(!ptr_func_uint){
        for(size_t i=5;i<w.size();){
            uint32_t n=w[i]>>16,op=w[i]&65535u;
            if(op==32 && n==4 && w[i+2]==7 && w[i+3]==uint32){ptr_func_uint=w[i+1];break;}
            i+=n;
        }
    }
    size_t vi=def[variable];if((w[vi]&65535u)!=59 || !def.count(w[vi+1]))return -1;
    size_t pi=def[w[vi+1]];if((w[pi]&65535u)!=32 || !def.count(w[pi+3]))return -1;
    size_t si=def[w[pi+3]];if((w[si]&65535u)!=27 || !def.count(w[si+2]))return -1;
    size_t ii=def[w[si+2]];if((w[ii]&65535u)!=25 || !def.count(w[ii+2]))return -1;
    size_t fi=def[w[ii+2]];if((w[fi]&65535u)!=22 || w[fi+2]!=32)return -1;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==23 && n==4 && w[i+3]==4){
            if(w[i+2]==w[ii+2])float4=w[i+1];
            if(w[i+2]==uint32)uint4=w[i+1];
        }
        i+=n;
    }
    if(!float4)return -1;
    uint32_t bound=w[3];
    auto types=xbg_integer_image_types(w,ii,pi,vi,uint32,true,bound);
    uint32_t image=types.image,sampled=types.sampled,pointer=types.pointer;
    bool add_vector=!uint4;if(add_vector)uint4=bound++;
    bool add_ptr_func=!ptr_func_uint;if(add_ptr_func)ptr_func_uint=bound++;

    std::unordered_set<uint32_t> loads,images;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==61 && n>=4 && w[i+3]==variable)loads.insert(w[i+2]);
        if(op==100 && n==4 && loads.count(w[i+3]))images.insert(w[i+2]);
        i+=n;
    }

    std::unordered_set<uint32_t> fetch_ids,target_vars;
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(op==95 && n>=5 && images.count(w[i+3])){
            uint32_t fid=w[i+2];fetch_ids.insert(fid);
            size_t next_i=i+n;
            if(next_i<w.size() && (w[next_i]&65535u)==81 && (w[next_i]>>16)>=5 && w[next_i+3]==fid){
                uint32_t ext_id=w[next_i+2];
                size_t store_i=next_i+(w[next_i]>>16);
                if(store_i<w.size() && (w[store_i]&65535u)==62 && (w[store_i]>>16)>=3 && w[store_i+2]==ext_id){
                    target_vars.insert(w[store_i+1]);
                }
            }
        }
        i+=n;
    }

    int fetches=0;
    std::vector<uint32_t> out(w.begin(),w.begin()+5);
    for(size_t i=5;i<w.size();){
        uint32_t n=w[i]>>16,op=w[i]&65535u;
        if(types.moved.count(i)){i+=n;continue;}
        if(i==vi){
            out.insert(out.end(),types.declarations.begin(),types.declarations.end());
            if(add_vector)out.insert(out.end(),{(4u<<16)|23u,uint4,uint32,4u});
            if(add_ptr_func)out.insert(out.end(),{(4u<<16)|32u,ptr_func_uint,7u,uint32});
        }
        size_t at=out.size();
        out.insert(out.end(),w.begin()+i,w.begin()+i+n);
        if(i==vi)out[at+1]=pointer;
        if(op==61 && loads.count(w[i+2]))out[at+1]=sampled;
        if(op==100 && images.count(w[i+2]))out[at+1]=image;
        if(op==59 && target_vars.count(w[i+2]))out[at+1]=ptr_func_uint;
        if(op==95 && n>=5 && images.count(w[i+3])){
            if(w[i+1]!=float4)return -1;
            out[at+1]=uint4;++fetches;
        }
        if(op==81 && n>=5 && fetch_ids.count(w[i+3])){
            out[at+1]=uint32;
        }
        if(op==61 && n>=4 && target_vars.count(w[i+3])){
            out[at+1]=uint32;
        }
        i+=n;
    }
    if(fetches<1)return -1;
    out[3]=bound;
    w.swap(out);
    return fetches;
}

// FFRS's stencil-channel extractor fetches an integer stencil texel through a
// translator-declared float image, then bitcasts the fetched bits back to int.
// Vulkan requires a uint sampled type for a stencil-aspect image view. Preserve
// every original operation by fetching uvec4 and bitcasting its bits to the old
// vec4 result. This exact bundle is proven by runtime STENCIL+RRRR bindings.
// Return 0 for other modules, 1 for the repair, -1 for an unexpected bundle.
inline int xbg_spirv_ffrs_stencil(std::vector<uint32_t> &words,uint32_t stage,const char *key){
    // Earth Shrine reflection/light lists: frame1358/draw131709 binds B64 as
    // R16_UINT, while the translated shader fetches float then bitcasts to an
    // integer light index. Binding numbers are local to each shader, so restrict
    // this repair to the captured bundle and retain its native R16 view/range.
    if(stage==4 && key && !std::strcmp(key,"97ae64dbc5e7ff26")){
        int n=xbg_spirv_shrine_lighting_fetch(words,64);return n==2?n:-1;
    }
    if((stage==5 || stage==0) && key){
        int changed=0;
        for(const auto &f:xbg_ffrs_integer_facts)if(!std::strcmp(key,f.key)){
            int n=f.storage?xbg_spirv_integer_storage(words,f.binding,f.scalar):xbg_spirv_integer_fetch(words,f.binding,f.scalar);if(n<1)return -1;changed+=n;
        }
        if(changed)return changed;
    }
    uint32_t binding=xbg_ffrs_uint_binding(stage,key);
    uint32_t store=xbg_ffrs_uint_store_binding(stage,key);
    if(store!=~0u){
        int fetched=xbg_spirv_uint_fetch(words,binding);if(fetched<1)return -1;
        int stored=xbg_spirv_uint_store(words,store);if(stored<1)return -1;return fetched+stored;
    }
    if(binding!=~0u && binding!=10)return xbg_spirv_uint_fetch(words,binding);
    if(stage!=4 || !key || std::strcmp(key,"1b69b9b28f994df3"))return 0;
    if(words.size()<5 || words[0]!=0x07230203u)return -1;
    uint32_t float32=0,uint32=0,float4=0,uint4=0,image=0,images=0,fetches=0;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;if(!n || i+n>words.size())return -1;
        if(op==22 && n==3 && words[i+2]==32)float32=words[i+1];
        if(op==21 && n==4 && words[i+2]==32 && words[i+3]==0)uint32=words[i+1];
        if(op==25){++images;image=words[i+1];}
        if(op==95)++fetches;i+=n;
    }
    if(!float32 || !uint32 || images!=1 || fetches!=1)return -1;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==23 && n==4 && words[i+3]==4){if(words[i+2]==float32)float4=words[i+1];if(words[i+2]==uint32)uint4=words[i+1];}i+=n;
    }
    if(!float4)return -1;
    uint32_t bound=words[3];bool add_vector=!uint4;if(add_vector)uint4=bound++;
    std::vector<uint32_t> out(words.begin(),words.begin()+5);bool inserted=!add_vector;
    for(size_t i=5;i<words.size();){uint32_t n=words[i]>>16,op=words[i]&65535u;
        if(op==54 && !inserted){out.insert(out.end(),{(4u<<16)|23u,uint4,uint32,4u});inserted=true;}
        size_t at=out.size();out.insert(out.end(),words.begin()+i,words.begin()+i+n);
        if(op==25 && words[i+1]==image){if(n<9 || words[i+2]!=float32)return -1;out[at+2]=uint32;}
        if(op==95){if(n<5 || words[i+1]!=float4)return -1;uint32_t result=words[i+2],raw=bound++;out[at+1]=uint4;out[at+2]=raw;out.insert(out.end(),{(4u<<16)|124u,float4,result,raw});}
        i+=n;
    }
    if(!inserted)return -1;out[3]=bound;words.swap(out);return 1;
}
