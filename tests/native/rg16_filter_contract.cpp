/* Self-owned RG16_UNORM data/shader only. Host: CPU precision/stride contracts.
 * Android --gpu: independent Vulkan filtering reference, no complete engine.
 * Android --gpu-proxy sample.spv vert.spv frag.spv [private-parent]: additionally
 * invokes real production Image/upload/native UNORM blending/lazy proxy helpers,
 * fences, reads every mip/layer and samples the actual refreshed proxy. Own
 * process only; no game assets/window/presentation or game compatibility claim.
 * --shader-source / --vertex-source / --fragment-source emit private GLSL.
 */
#include "../../src/graphics/nvn_vulkan/engine/xbg_texture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
extern "C" {
#include "../../src/graphics/nvn_vulkan/tex/nvn_deswizzle.h"
}
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __ANDROID__
#include "../../src/graphics/nvn_vulkan/engine/xbg_sampler.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_game.h"
#include <sys/stat.h>
#include <unistd.h>
extern "C" int xbg_rg16_proxy_contract(const uint32_t*,size_t,const uint32_t*,size_t,const uint32_t*,size_t,const uint16_t*,size_t,const void*,size_t,float*,float*,uint16_t*,float*,float*,uint64_t*);
#endif
/* Optional host profiler only; no graphics work/completion is replaced. */
extern "C" void dm_prof_add(int,uint64_t,uint64_t) {}

static const char vertex_source[]=R"glsl(#version 450
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.-1.,0.,1.);}
)glsl";
static const char fragment_source[]=R"glsl(#version 450
layout(push_constant) uniform Color {vec4 value;} color;
layout(location=0) out vec4 result;
void main(){result=color.value;}
)glsl";
static const char shader_source[]=R"glsl(#version 450
layout(local_size_x=1) in;
layout(set=0,binding=0) uniform sampler2DArray image_rg;
layout(set=0,binding=1,std430) buffer Results { vec4 values[]; } result;
layout(push_constant) uniform Request { float u; float v; float lod; uint layer; uint index; } q;
void main() { result.values[q.index]=textureLod(image_rg,vec3(q.u,q.v,float(q.layer)),q.lod); }
)glsl";
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Mip {
    uint32_t w,h,layers;size_t row,image;
    std::vector<uint8_t> blocks;
    std::vector<uint16_t> reference;
    std::vector<float> decoded;
};
static Mip make_mip(uint32_t w,uint32_t h,uint32_t layers,unsigned seed){
    Mip m{};m.w=w;m.h=h;m.layers=layers;m.row=size_t(w)*4+8;m.image=m.row*h+16;
    m.blocks.assign(m.image*layers,0x9d);m.reference.resize(size_t(w)*h*layers*2);
    for(unsigned z=0;z<layers;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<2;c++){
        // Strong spatial contrast distinguishes LINEAR from forbidden NEAREST;
        // separate mip plateaus distinguish min3 mip-nearest from trilinear.
        const uint16_t bases[3]={3000,45000,16000},amplitudes[3]={26000,12000,10000};
        const unsigned pattern=(seed+2)%3;
        const uint16_t red=uint16_t(bases[pattern]+((x+y+z)&1)*amplitudes[pattern]);
        const uint16_t value=c?uint16_t(65535-red):red;
        std::memcpy(m.blocks.data()+z*m.image+y*m.row+x*4+c*2,&value,2);
        m.reference[((size_t(z)*h+y)*w+x)*2+c]=value;
    }
    m.decoded.resize(m.reference.size());const auto original=m.blocks;
    require(xbg_rg16_unorm_float_rows(w,h,layers,m.blocks.data(),m.blocks.size(),m.row,m.image,
        m.decoded.data(),m.decoded.size()*4,0,0,1)==0,"strided RG16 upload conversion failed");
    for(size_t i=0;i<m.reference.size();i++)require(m.decoded[i]==float(double(m.reference[i])/65535.0),"RG32 differs from independent UNORM16 reference");
    std::vector<uint8_t> restored(m.blocks.size(),0x9d);
    require(xbg_rg16_unorm_float_rows(w,h,layers,m.decoded.data(),m.decoded.size()*4,0,0,
        restored.data(),restored.size(),m.row,m.image,0)==0,"strided RG16 readback conversion failed");
    require(restored==m.blocks,"strided readback changed original values or row/image padding");
    require(original==m.blocks,"normalization changed original guest source");return m;
}
struct Request {float u,v,lod;uint32_t layer,index;};
static double sample_level(const Mip &m,float u,float v,unsigned layer,unsigned channel){
    const double px=double(u)*m.w-.5,py=double(v)*m.h-.5;
    const int x=int(std::floor(px)),y=int(std::floor(py));
    const double fx=px-x,fy=py-y;
    auto at=[&](int ix,int iy){ix=std::max(0,std::min(int(m.w)-1,ix));iy=std::max(0,std::min(int(m.h)-1,iy));
        return double(m.reference[((size_t(layer)*m.h+unsigned(iy))*m.w+unsigned(ix))*2+channel])/65535.0;};
    return (at(x,y)*(1-fx)+at(x+1,y)*fx)*(1-fy)+(at(x,y+1)*(1-fx)+at(x+1,y+1)*fx)*fy;
}
static std::vector<Request> make_requests(){
    std::vector<Request> requests;
    for(unsigned layer=0;layer<2;layer++)for(float lod:{0.f,.25f,.75f,1.f,1.25f,1.75f,2.f})for(unsigned uv=0;uv<4;uv++)
        requests.push_back({uv==0?-.1f:uv==1?.31f:uv==2?.73f:1.1f,uv==0?.22f:uv==1?.67f:uv==2?.41f:.9f,lod,layer,uint32_t(requests.size())});
    return requests;
}
struct Discrimination {unsigned nearest=0,trilinear=0;double max_nearest=0,max_trilinear=0,min_rejected=1;};
static Discrimination discriminate(const std::vector<Mip> &mips,const std::vector<Request> &requests,double tolerance){
    Discrimination result;
    for(const auto &q:requests){
        const unsigned level=unsigned(std::floor(q.lod+.5f)),base=unsigned(std::floor(q.lod));
        const Mip &m=mips[level];const double fraction=q.lod-base;
        const unsigned x=unsigned(std::max(0,std::min(int(m.w)-1,int(std::floor(q.u*m.w)))));
        const unsigned y=unsigned(std::max(0,std::min(int(m.h)-1,int(std::floor(q.v*m.h)))));
        for(unsigned channel=0;channel<2;channel++){
            const double expected=sample_level(m,q.u,q.v,q.layer,channel);
            const double nearest=double(m.reference[((size_t(q.layer)*m.h+y)*m.w+x)*2+channel])/65535.0;
            const double trilinear=sample_level(mips[base],q.u,q.v,q.layer,channel)*(1-fraction)+sample_level(mips[std::min(2u,base+1)],q.u,q.v,q.layer,channel)*fraction;
            const double ng=std::fabs(expected-nearest),tg=std::fabs(expected-trilinear);
            result.max_nearest=std::max(result.max_nearest,ng);result.max_trilinear=std::max(result.max_trilinear,tg);
            if(ng>4*tolerance){++result.nearest;result.min_rejected=std::min(result.min_rejected,ng);}
            if(fraction>0&&tg>4*tolerance){++result.trilinear;result.min_rejected=std::min(result.min_rejected,tg);}
        }
    }
    return result;
}
static void require_discrimination(const Discrimination &d,double tolerance,const char *phase){
    std::printf("rg16_filter_contract %s counterfactuals nearest=%u wrong_trilinear=%u max_nearest_gap=%.9g max_trilinear_gap=%.9g min_rejected_gap=%.9g rejection_threshold=%.9g tolerance=%.9g\n",
        phase,d.nearest,d.trilinear,d.max_nearest,d.max_trilinear,d.min_rejected,4*tolerance,tolerance);
    std::fflush(stdout);
    require(d.nearest>=4&&d.trilinear>=4,"fixture cannot distinguish forbidden nearest/wrong-trilinear substitutions");
}

static void cpu_contract(){
    std::vector<uint16_t> words(65536*2),restored(words.size());std::vector<float> floats(words.size());
    for(unsigned i=0;i<65536;i++){words[i*2]=uint16_t(i);words[i*2+1]=uint16_t(65535-i);}
    const auto original=words;
    require(xbg_rg16_unorm_to_rg32_float(words.data(),words.size()*2,floats.data(),floats.size()*4,65536)==0,"full-domain normalize failed");
    require(xbg_rg32_float_to_rg16_unorm(floats.data(),floats.size()*4,restored.data(),restored.size()*2,65536)==0,"full-domain readback quantization failed");
    require(restored==words&&words==original,"all 65536 values must survive exact normalized roundtrip");
    for(unsigned i=0;i<65536;i++){
        require(floats[i*2]==float(double(i)/65535.0),"normalization mathematical reference mismatch");
        if(i)require(floats[i*2]>floats[(i-1)*2],"half/RG8 precision collapsed original levels");
    }
    std::vector<Mip> mips;for(unsigned level=0;level<3;level++)mips.push_back(make_mip(std::max(1u,7u>>level),std::max(1u,5u>>level),3,level+1));
    // Target Mali has 8-bit subtexel/mipmap precision (previous real GPU
    // contract tolerance). GPU mode recomputes from the actual device limits.
    const double fixture_tolerance=3.0/256.0+1e-6;
    require_discrimination(discriminate(mips,make_requests(),fixture_tolerance),fixture_tolerance,"CPU");
    nvn_texdesc desc{};desc.target=4;desc.format=0x12;desc.width=7;desc.height=5;desc.depth=3;desc.levels=3;
    nvn_texlayout layout{};nvn_tex_layout(&desc,&layout);require(layout.bpb==4&&layout.layers==3,"NVN0x12 original layout must remain RG16/bpp4");
    std::vector<uint8_t> storage(layout.size,0xa5);
    for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++){
        const auto &m=mips[level];require(nvn_swizzle(&desc,level,layer,m.blocks.data()+layer*m.image,m.row,storage.data())==0,"NVN mip/layer swizzle fixture failed");}
    const auto original_storage=storage;
    for(unsigned layer=0;layer<3;layer++)for(unsigned level=0;level<3;level++){
        const auto &m=mips[level];std::vector<uint16_t> packed(size_t(m.w)*m.h*2);std::vector<float> normalized(packed.size());
        require(nvn_deswizzle(&desc,level,layer,storage.data(),packed.data(),m.w*4)==0,"original 4byte NVN deswizzle failed");
        require(xbg_rg16_unorm_to_rg32_float(packed.data(),packed.size()*2,normalized.data(),normalized.size()*4,size_t(m.w)*m.h)==0,"mip normalization failed");
        require(std::equal(normalized.begin(),normalized.end(),m.decoded.begin()+size_t(layer)*m.w*m.h*2),"mip/layer normalization reference mismatch");}
    require(storage==original_storage,"sampling preparation changed original block-linear backing");
    /* Partial source rectangle with different source and physical pitches;
     * inverse transfer must touch only the requested region of guest output. */
    const auto &m=mips[0];const unsigned rw=3,rh=2;const size_t destination_row=7*8+8,destination_image=destination_row*5+16;
    std::vector<uint8_t> converted(destination_image*2,0x6b);
    require(xbg_rg16_unorm_float_rows(rw,rh,2,m.blocks.data()+m.row+2*4,m.blocks.size()-m.row-2*4,m.row,m.image,
        converted.data()+destination_row+2*8,converted.size()-destination_row-2*8,destination_row,destination_image,1)==0,"partial strided upload failed");
    std::vector<uint8_t> partial(m.blocks.size(),0x37),expected=partial;
    require(xbg_rg16_unorm_float_rows(rw,rh,2,converted.data()+destination_row+2*8,converted.size()-destination_row-2*8,destination_row,destination_image,
        partial.data()+m.row+2*4,partial.size()-m.row-2*4,m.row,m.image,0)==0,"partial strided readback failed");
    for(unsigned z=0;z<2;z++)for(unsigned y=0;y<rh;y++)std::memcpy(expected.data()+z*m.image+(y+1)*m.row+2*4,m.blocks.data()+z*m.image+(y+1)*m.row+2*4,rw*4);
    require(partial==expected,"partial readback touched bytes outside rectangle or lost original values");
    auto invalid=[&](unsigned w,unsigned h,unsigned d,size_t source_bytes,size_t row,size_t image,size_t out_bytes){
        std::vector<uint8_t> output(4096,0x67);
        require(xbg_rg16_unorm_float_rows(w,h,d,m.blocks.data(),source_bytes,row,image,output.data(),out_bytes,0,0,1)!=0,"invalid row transfer bounds accepted");
        require(std::all_of(output.begin(),output.end(),[](uint8_t v){return v==0x67;}),"rejected transfer changed output");};
    invalid(0,5,3,m.blocks.size(),m.row,m.image,4096);
    invalid(7,0,3,m.blocks.size(),m.row,m.image,4096);
    invalid(7,5,0,m.blocks.size(),m.row,m.image,4096);
    invalid(UINT32_MAX,UINT32_MAX,UINT32_MAX,m.blocks.size(),m.row,m.image,4096);
    invalid(7,5,3,m.blocks.size(),27,m.image,4096);
    invalid(7,5,3,m.blocks.size(),m.row,m.row*4,4096);
    invalid(7,5,3,2*m.image+4*m.row+27,m.row,m.image,4096);
    invalid(7,5,3,m.blocks.size(),m.row,m.image,7*5*3*8-1);
    for(float bad:{-0.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        float source[4]={0,.5f,1,bad};uint16_t destination[4]={0x6b6b,0x6b6b,0x6b6b,0x6b6b};
        require(xbg_rg32_float_to_rg16_unorm(source,sizeof source,destination,sizeof destination,2)!=0,"unquantized invalid float accepted");
        for(auto word:destination)require(word==0x6b6b,"invalid float partially changed readback output");}
    require(xbg_rg32_float_to_rg16_unorm(floats.data(),7,restored.data(),4,1)!=0,"short float source accepted");
    require(xbg_rg32_float_to_rg16_unorm(floats.data(),8,restored.data(),3,1)!=0,"short original-bpp destination accepted");
    require(xbg_rg32_float_to_rg16_unorm(floats.data(),8,floats.data(),8,1)!=0,"overlapping inverse conversion accepted");
    require(xbg_rg32_float_to_rg16_unorm(nullptr,8,restored.data(),4,1)!=0,"null inverse source accepted");
    std::printf("rg16_filter_contract CPU PASS normalized_values=65536 exact_roundtrip=1 guest_bpp=4 image_bpp=8 mips=3 layers=3 non_square=7x5 partial_strides=1 negative_bounds=1 source_unchanged=1\n");
}

#ifdef __ANDROID__
static void vk_check(VkResult result,const char *call){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(call)+" VkResult="+std::to_string(result));}
#define VK_TEST(call) vk_check((call),#call)
struct HostBuffer {VkBuffer buffer{};VkDeviceMemory memory{};void *mapped{};};
static void gpu_contract(const char *shader_path,const char *vertex_path=nullptr,const char *fragment_path=nullptr,const char *private_parent="/data/local/tmp"){
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;
    VkInstance instance;VK_TEST(vkCreateInstance(&ici,nullptr,&instance));
    uint32_t count=0;VK_TEST(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count>0,"no Vulkan device");
    std::vector<VkPhysicalDevice> physicals(count);VK_TEST(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));
    VkPhysicalDevice gpu=physicals[0];VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(gpu,&props);
    VkFormatProperties old{},chosen{};vkGetPhysicalDeviceFormatProperties(gpu,VK_FORMAT_R16G16_UNORM,&old);
    vkGetPhysicalDeviceFormatProperties(gpu,VK_FORMAT_R32G32_SFLOAT,&chosen);
    require(!(old.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),"target does not reproduce the old RG16 linear-filter rejection");
    const VkFormatFeatureFlags needed=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    require((chosen.optimalTilingFeatures&needed)==needed,"RG32 lacks actual sampled+linear capability");
    std::printf("rg16_filter_contract old_path=REJECTED_BY_REAL_FORMAT_CAPS vk77_features=%#x new_vk103_features=%#x device=%s\n",old.optimalTilingFeatures,chosen.optimalTilingFeatures,props.deviceName);
    uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);std::vector<VkQueueFamilyProperties> families(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families.data());unsigned family=0;
    while(family<nq&&((families[family].queueFlags&(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))!=(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT)))family++;require(family<nq,"no compute queue");
    float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
    VkDevice device;VK_TEST(vkCreateDevice(gpu,&dci,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    VkPhysicalDeviceMemoryProperties memories;vkGetPhysicalDeviceMemoryProperties(gpu,&memories);
    auto memory_type=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(unsigned i=0;i<memories.memoryTypeCount;i++)
        if((bits&(1u<<i))&&(memories.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("no required Vulkan memory type");};
    auto host_buffer=[&](VkDeviceSize bytes,VkBufferUsageFlags usage){HostBuffer b;
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;VK_TEST(vkCreateBuffer(device,&ci,nullptr,&b.buffer));
        VkMemoryRequirements r;vkGetBufferMemoryRequirements(device,b.buffer,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;ai.memoryTypeIndex=memory_type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_TEST(vkAllocateMemory(device,&ai,nullptr,&b.memory));VK_TEST(vkBindBufferMemory(device,b.buffer,b.memory,0));VK_TEST(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped));return b;};
    std::vector<Mip> mips;for(unsigned level=0;level<3;level++)mips.push_back(make_mip(std::max(1u,7u>>level),std::max(1u,5u>>level),2,level+1));
    std::vector<VkBufferImageCopy> copies;size_t upload_bytes=0;
    for(unsigned level=0;level<3;level++){VkBufferImageCopy c{};c.bufferOffset=upload_bytes;c.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,2};c.imageExtent={mips[level].w,mips[level].h,1};copies.push_back(c);upload_bytes+=mips[level].decoded.size()*4;}
    HostBuffer staging=host_buffer(upload_bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    HostBuffer texture_readback=host_buffer(upload_bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);std::memset(texture_readback.mapped,0xcd,upload_bytes);
    for(unsigned level=0;level<3;level++)std::memcpy(static_cast<uint8_t*>(staging.mapped)+copies[level].bufferOffset,mips[level].decoded.data(),mips[level].decoded.size()*4);
    const auto requests=make_requests();
    HostBuffer results=host_buffer(requests.size()*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);std::memset(results.mapped,0xcd,requests.size()*16);
    VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};im.imageType=VK_IMAGE_TYPE_2D;im.format=VK_FORMAT_R32G32_SFLOAT;im.extent={7,5,1};im.mipLevels=3;im.arrayLayers=2;im.samples=VK_SAMPLE_COUNT_1_BIT;im.tiling=VK_IMAGE_TILING_OPTIMAL;im.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage image;VK_TEST(vkCreateImage(device,&im,nullptr,&image));VkMemoryRequirements ir;vkGetImageMemoryRequirements(device,image,&ir);
    VkMemoryAllocateInfo ia{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ia.allocationSize=ir.size;ia.memoryTypeIndex=memory_type(ir.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory image_memory;VK_TEST(vkAllocateMemory(device,&ia,nullptr,&image_memory));VK_TEST(vkBindImageMemory(device,image,image_memory,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vi.format=im.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,2};
    VkImageView view;VK_TEST(vkCreateImageView(device,&vi,nullptr,&view));
    xbg_nvn_sampler_desc input{3,1,7,7,7,0,2,0,2,0,1,{0,0,0,0}};
    xbg_sampler_limits limits{VK_FALSE,VK_FALSE,VK_FALSE,1,props.limits.maxSamplerLodBias};VkSamplerCreateInfo si{};
    require(xbg_sampler_from_nvn(&input,&limits,&si)==0,"actual NVN min3/mag1 sampler conversion failed");
    require(si.minFilter==VK_FILTER_LINEAR&&si.magFilter==VK_FILTER_LINEAR&&si.mipmapMode==VK_SAMPLER_MIPMAP_MODE_NEAREST,"NVN min3 lost mip-nearest linear filtering");
    VkSampler sampler;VK_TEST(vkCreateSampler(device,&si,nullptr,&sampler));
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=2;li.pBindings=bindings;
    VkDescriptorSetLayout layout;VK_TEST(vkCreateDescriptorSetLayout(device,&li,nullptr,&layout));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Request)};VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=1;pli.pSetLayouts=&layout;pli.pushConstantRangeCount=1;pli.pPushConstantRanges=&push;
    VkPipelineLayout pipeline_layout;VK_TEST(vkCreatePipelineLayout(device,&pli,nullptr,&pipeline_layout));
    std::ifstream file(shader_path,std::ios::binary|std::ios::ate);require(bool(file),"cannot open self-owned shader SPIRV");auto bytes=file.tellg();require(bytes>=20&&size_t(bytes)%4==0,"invalid SPIRV size");std::vector<uint32_t> code(size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(code.data()),bytes);require(bool(file)&&code[0]==0x07230203,"invalid SPIRV input");
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smi.codeSize=code.size()*4;smi.pCode=code.data();VkShaderModule module;VK_TEST(vkCreateShaderModule(device,&smi,nullptr,&module));
    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pci.stage.module=module;pci.stage.pName="main";pci.layout=pipeline_layout;
    VkPipeline pipeline;VK_TEST(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline));
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dpi.maxSets=1;dpi.poolSizeCount=2;dpi.pPoolSizes=sizes;
    VkDescriptorPool descriptors;VK_TEST(vkCreateDescriptorPool(device,&dpi,nullptr,&descriptors));VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sai.descriptorPool=descriptors;sai.descriptorSetCount=1;sai.pSetLayouts=&layout;VkDescriptorSet set;VK_TEST(vkAllocateDescriptorSets(device,&sai,&set));
    VkDescriptorImageInfo ii{sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkDescriptorBufferInfo bi{results.buffer,0,VK_WHOLE_SIZE};VkWriteDescriptorSet writes[2]{};
    for(unsigned i=0;i<2;i++){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;}writes[0].pImageInfo=&ii;writes[1].pBufferInfo=&bi;vkUpdateDescriptorSets(device,2,writes,0,nullptr);
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpi.queueFamilyIndex=family;VkCommandPool pool;VK_TEST(vkCreateCommandPool(device,&cpi,nullptr,&pool));VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;VkCommandBuffer command;VK_TEST(vkAllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VK_TEST(vkBeginCommandBuffer(command,&begin));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.subresourceRange=vi.subresourceRange;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    vkCmdCopyBufferToImage(command,staging.buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies.size()),copies.data());
    barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,nullptr);
    for(const auto &request:requests){vkCmdPushConstants(command,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof request,&request);vkCmdDispatch(command,1,1,1);}
    barrier.oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_SHADER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,texture_readback.buffer,uint32_t(copies.size()),copies.data());
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);VK_TEST(vkEndCommandBuffer(command));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;VK_TEST(vkCreateFence(device,&fci,nullptr,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;VK_TEST(vkQueueSubmit(queue,1,&submit,fence));VK_TEST(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
    const float *actual=static_cast<const float*>(results.mapped);double maximum_error=0;
    const double tolerance=2*std::ldexp(1.,-int(props.limits.subTexelPrecisionBits))+std::ldexp(1.,-int(props.limits.mipmapPrecisionBits))+1e-6;
    const Discrimination counterfactuals=discriminate(mips,requests,tolerance);
    require_discrimination(counterfactuals,tolerance,"GPU-device-limits");
    const unsigned rejects_nearest=counterfactuals.nearest,rejects_mip_nearest=counterfactuals.trilinear;
    for(const auto &q:requests){unsigned lo=unsigned(std::floor(q.lod+.5f)),hi=lo;double blend=0;
        for(unsigned channel=0;channel<4;channel++){
            double expected=channel==2?0:channel==3?1:sample_level(mips[lo],q.u,q.v,q.layer,channel)*(1-blend)+sample_level(mips[hi],q.u,q.v,q.layer,channel)*blend;
            double error=std::fabs(double(actual[q.index*4+channel])-expected);maximum_error=std::max(maximum_error,error);
            if(!(error<=(channel<2?tolerance:1e-6))){std::fprintf(stderr,"query=%u channel=%u lod=%g actual=%g reference=%.10g error=%g tolerance=%g\n",q.index,channel,q.lod,actual[q.index*4+channel],expected,error,tolerance);throw std::runtime_error("actual GPU min3 linear/mip filtering differs from mathematical reference");}
        }
    }
    size_t readback_values=0;
    for(unsigned level=0;level<3;level++){
        const auto &m=mips[level];std::vector<uint16_t> original_values(m.reference.size());
        require(xbg_rg16_unorm_float_rows(m.w,m.h,m.layers,static_cast<const uint8_t*>(texture_readback.mapped)+copies[level].bufferOffset,m.decoded.size()*4,0,0,
            original_values.data(),original_values.size()*2,0,0,0)==0,"actual GPU texture readback did not quantize to original bpp4");
        require(original_values==m.reference,"actual GPU mip/layer readback lost an original UNORM16 value");readback_values+=original_values.size();
    }
    std::printf("rg16_filter_contract GPU readback exact_unorm16_values=%zu original_bpp=4 physical_bpp=8\n",readback_values);
    VK_TEST(vkDeviceWaitIdle(device));vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDescriptorPool(device,descriptors,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);vkDestroyPipelineLayout(device,pipeline_layout,nullptr);vkDestroyDescriptorSetLayout(device,layout,nullptr);vkDestroySampler(device,sampler,nullptr);vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,image_memory,nullptr);
    for(auto b:{staging,results,texture_readback}){vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,nullptr);vkFreeMemory(device,b.memory,nullptr);}if(vertex_path){
        auto load=[](const char *path){std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"cannot open self-owned render SPIRV");size_t n=size_t(f.tellg());require(n>=20&&n%4==0,"invalid render SPIRV size");std::vector<uint32_t> r(n/4);f.seekg(0);f.read(reinterpret_cast<char*>(r.data()),n);require(bool(f)&&r[0]==0x07230203,"invalid render SPIRV");return r;};
        auto vertex=load(vertex_path),fragment=load(fragment_path);
        std::string pattern=std::string(private_parent)+"/ffrs-rg16-proxy-XXXXXX";std::vector<char> tmp(pattern.begin(),pattern.end());tmp.push_back(0);require(mkdtemp(tmp.data())!=nullptr,"cannot create private engine component directory");std::string shader_dir=std::string(tmp.data())+"/shaders";require(mkdir(shader_dir.c_str(),0700)==0,"cannot create empty component shader directory");
        xbg_game_config config{};config.size=sizeof config;config.physical=gpu;config.device=device;config.queue=queue;config.queue_family=family;config.fence_timeout_ns=5000000000ull;config.shader_directory=shader_dir.c_str();
        config.present=[](void*,void*,VkImage,VkFormat,uint32_t,uint32_t,const int32_t[4],int32_t)->int{std::fputs("unexpected component presentation\n",stderr);std::abort();};
        config.log=[](void*,const char *line){std::fprintf(stderr,"[actual-engine] %s\n",line);};
        config.fatal=[](void*,const char *line){std::fprintf(stderr,"[actual-engine] FATAL %s\n",line);std::abort();};
        char error[1024]{};require(xbg_game_initialize(&config,error,sizeof error)==0,error);
        std::vector<uint16_t> input,expected;for(const auto &m:mips)input.insert(input.end(),m.reference.begin(),m.reference.end());expected=input;
        std::vector<float> initial(input.size()),final_values(input.size()),cleared(input.size()),proxy_samples(requests.size()*4);std::vector<uint16_t> canonical(input.size());uint64_t counters[4]{};
        require(xbg_rg16_proxy_contract(vertex.data(),vertex.size()*4,fragment.data(),fragment.size()*4,code.data(),code.size()*4,input.data(),input.size(),requests.data(),requests.size(),initial.data(),final_values.data(),canonical.data(),proxy_samples.data(),cleared.data(),counters)==0,"production Image canonical/proxy contract entry failed");
        auto quant=[](double value){return uint16_t(std::floor(std::max(0.,std::min(1.,value))*65535.+.5));};
        const float colors[2]={.6f/65535.f,1.6f/65535.f};const float alpha=.8f;uint16_t correct[2]{},preblend[2]{};double float_target[2]{};
        for(unsigned draw=0;draw<32;draw++)for(unsigned c=0;c<2;c++){correct[c]=quant(double(correct[c])/65535.+double(colors[c])*alpha);preblend[c]=quant(double(preblend[c])/65535.+double(quant(colors[c]))/65535.*alpha);float_target[c]+=double(colors[c])*alpha;}
        for(unsigned c=0;c<2;c++)require(std::abs(int(correct[c])-int(preblend[c]))>=8&&std::abs(int(correct[c])-int(quant(float_target[c])))>=8,"UNORM draw fixture cannot kill preblend/final-only quantization");
        for(unsigned y=0;y<5;y++)for(unsigned x=0;x<7;x++)for(unsigned c=0;c<2;c++)expected[(size_t(y)*7+x)*2+c]=correct[c];
        double max_proxy_error=0;unsigned max_native_error=0;
        for(size_t i=0;i<input.size();i++){
            require(std::fabs(double(initial[i])-double(input[i])/65535.)<=1e-7,"initial production proxy differs from original UNORM texels");
            const unsigned native_error=unsigned(std::abs(int(canonical[i])-int(expected[i])));max_native_error=std::max(max_native_error,native_error);require(native_error<=1,"native UNORM per-draw blending differs from independent quantize-after-each-draw reference");
            const double proxy_error=std::fabs(double(final_values[i])-double(canonical[i])/65535.);max_proxy_error=std::max(max_proxy_error,proxy_error);require(proxy_error<=1e-7,"production proxy did not preserve actual canonical UNORM values");
            const double wanted=(i==input.size()-2?double(quant(.25)):i==input.size()-1?double(quant(.75)):double(canonical[i]))/65535.;require(std::fabs(double(cleared[i])-wanted)<=1.1/65535.,"non-render clear left stale/wrong mip/layer proxy contents");
        }
        require(counters[0]==3&&counters[1]==9&&counters[2]==counters[3],"production proxy refresh/version cache counts differ");
        auto filtered_mips=mips;size_t base=0;for(auto &m:filtered_mips){for(auto &v:m.reference)v=expected[base++];}
        const auto kills=discriminate(filtered_mips,requests,tolerance);require_discrimination(kills,tolerance,"production-proxy");double max_filter_error=0;
        for(const auto &q:requests)for(unsigned c=0;c<4;c++){const unsigned level=unsigned(std::floor(q.lod+.5f));const double wanted=c==2?0:c==3?1:sample_level(filtered_mips[level],q.u,q.v,q.layer,c);const double e=std::fabs(double(proxy_samples[q.index*4+c])-wanted);max_filter_error=std::max(max_filter_error,e);require(e<=(c<2?tolerance:1e-6),"actual production proxy min3/mag1 filtering differs from independent reference");}
        std::printf("rg16_filter_contract canonical_proxy PASS full_engine_Image_path=INVOKED canonical_vk=77 canonical_bpp=4 sampled_vk=103 sampled_bpp=8 negative_pool_null=1 native_draws=32 canonical_values=%zu refreshes=%llu mip_dispatches=%llu stable_view=1 cached_reuses=12 non_render_clear=1 max_native_error_u16=%u max_proxy_error=%.9g filtered_samples=%zu max_filter_error=%.9g rejects_preblend=%u/%u rejects_final_only=%u/%u shaderStorageImageExtendedFormats_enabled=0 presents=0\n",input.size(),(unsigned long long)counters[0],(unsigned long long)counters[1],max_native_error,max_proxy_error,requests.size(),max_filter_error,unsigned(std::abs(int(correct[0])-int(preblend[0]))),unsigned(std::abs(int(correct[1])-int(preblend[1]))),unsigned(std::abs(int(correct[0])-int(quant(float_target[0])))),unsigned(std::abs(int(correct[1])-int(quant(float_target[1])))));
        require(xbg_game_shutdown()==0,"production component engine shutdown failed");
    }
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::printf("rg16_filter_contract GPU PASS samples=%zu min=3 mag=1 bilinear=1 mip_nearest=1 mips=3 layers=2 B=0 A=1 max_error=%.9g tolerance=%.9g rejects_nearest=%u rejects_wrong_trilinear=%u full_engine_Image_path=NOT_INVOKED\n",requests.size(),maximum_error,tolerance,rejects_nearest,rejects_mip_nearest);
}
#endif
int main(int argc,char **argv){
    try{
        if(argc==2&&std::strcmp(argv[1],"--vertex-source")==0){std::fputs(vertex_source,stdout);return 0;}
        if(argc==2&&std::strcmp(argv[1],"--fragment-source")==0){std::fputs(fragment_source,stdout);return 0;}
        if(argc==2&&std::strcmp(argv[1],"--shader-source")==0){std::fputs(shader_source,stdout);return 0;}
        cpu_contract();
        if((argc==5||argc==6)&&std::strcmp(argv[1],"--gpu-proxy")==0){
#ifdef __ANDROID__
            gpu_contract(argv[2],argv[3],argv[4],argc==6?argv[5]:"/data/local/tmp");return 0;
#else
            throw std::runtime_error("production GPU proxy component requires Android Vulkan device");
#endif
        }
        if(argc==3&&std::strcmp(argv[1],"--gpu")==0){
#ifdef __ANDROID__
            gpu_contract(argv[2]);
#else
            throw std::runtime_error("GPU component must run on actual Android Vulkan device");
#endif
        }else require(argc==1,"usage: contract [--shader-source | --gpu self-owned.spv]");
        return 0;
    }catch(const std::exception &error){std::fprintf(stderr,"rg16_filter_contract FAIL %s\n",error.what());return 1;}
}
