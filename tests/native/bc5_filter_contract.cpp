/* Self-owned BC5 data/shader only. Host: CPU precision/stride contracts.
 * Android --gpu shader.spv: actual RG32 image, production decode/normalization
 * and NVN sampler helpers, real bilinear/trilinear sampling and SSBO readback.
 * This component does not invoke the complete xbg_game Image/descriptor path;
 * that path is compiled and must also pass the coordinator's game regression.
 * --shader-source emits the self-owned GLSL for private offline compilation.
 */
#include "../../src/graphics/nvn_vulkan/engine/xbg_texture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __ANDROID__
#include "../../src/graphics/nvn_vulkan/engine/xbg_sampler.h"
#endif
/* Optional host profiler only; no graphics work/completion is replaced. */
extern "C" void dm_prof_add(int,uint64_t,uint64_t) {}

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
static uint16_t reference_channel(const uint8_t *block,unsigned pixel){
    uint64_t selectors=0;for(unsigned k=0;k<6;k++)selectors|=uint64_t(block[2+k])<<(8*k);
    const unsigned index=unsigned(selectors>>(3*pixel))&7;
    const uint32_t a=uint32_t(block[0])*257,b=uint32_t(block[1])*257;
    if(index==0)return uint16_t(a);if(index==1)return uint16_t(b);
    if(block[0]>block[1])return uint16_t(((8-index)*a+(index-1)*b+3)/7);
    if(index<6)return uint16_t(((6-index)*a+(index-1)*b+2)/5);
    return index==6?0:65535;
}
static Mip make_mip(uint32_t w,uint32_t h,uint32_t layers,unsigned seed){
    Mip m{};m.w=w;m.h=h;m.layers=layers;
    const size_t columns=(w+3)/4,rows=(h+3)/4;
    m.row=columns*16+16;m.image=m.row*rows+16;
    m.blocks.assign(m.image*layers,0x9d);
    for(unsigned layer=0;layer<layers;layer++)for(size_t y=0;y<rows;y++)for(size_t x=0;x<columns;x++){
        uint8_t *block=m.blocks.data()+layer*m.image+y*m.row+x*16;
        for(unsigned c=0;c<2;c++){
            uint8_t *channel=block+c*8;
            channel[0]=uint8_t(29+((seed*23+layer*59+x*41+y*13+c*37)%200));
            channel[1]=uint8_t(241-((seed*17+layer*31+x*19+y*47+c*53)%200));
            uint64_t indices=0;for(unsigned p=0;p<16;p++)indices|=uint64_t((p+seed+layer+3*c+x+y)%8)<<(3*p);
            for(unsigned k=0;k<6;k++)channel[2+k]=uint8_t(indices>>(8*k));
        }
    }
    m.reference.resize(size_t(w)*h*layers*2);
    for(unsigned z=0;z<layers;z++)for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<2;c++){
        const uint8_t *block=m.blocks.data()+z*m.image+(y/4)*m.row+(x/4)*16+c*8;
        m.reference[((size_t(z)*h+y)*w+x)*2+c]=reference_channel(block,(y%4)*4+x%4);
    }
    m.decoded.resize(size_t(w)*h*layers*2);
    const auto original=m.blocks;
    std::vector<uint16_t> existing(m.reference.size());
    require(xbg_decode_bc_linear(0x4b,w,h,layers,m.blocks.data(),m.blocks.size(),m.row,m.image,
        existing.data(),existing.size()*sizeof(uint16_t))==0,"existing BC5 RG16 decoder failed");
    require(existing==m.reference,"existing BC5 precision disagrees with palette reference");
    require(xbg_decode_bc5_rg32_linear(w,h,layers,m.blocks.data(),m.blocks.size(),m.row,m.image,
        m.decoded.data(),m.decoded.size()*sizeof(float))==0,"BC5 RG32 decode failed");
    require(m.blocks==original,"source compressed bytes changed");
    for(size_t i=0;i<m.decoded.size();i++)
        require(m.decoded[i]==float(double(m.reference[i])/65535.0),"BC5 decode differs from independent palette reference");
    return m;
}
static void cpu_contract(){
    std::vector<uint16_t> words(65536*2);std::vector<float> floats(words.size());
    for(unsigned i=0;i<65536;i++){words[i*2]=uint16_t(i);words[i*2+1]=uint16_t(65535-i);}
    const auto original=words;
    require(xbg_rg16_unorm_to_rg32_float(words.data(),words.size()*2,floats.data(),floats.size()*4,65536)==0,"full-domain normalize failed");
    for(unsigned i=0;i<65536;i++){
        require(floats[i*2]==float(double(i)/65535.0),"normalization differs from float32 mathematical reference");
        require(std::lround(double(floats[i*2])*65535.0)==i,"normalized value lost a 16-bit source level");
        require(floats[i*2+1]==float(double(65535-i)/65535.0),"green normalization incorrect");
        if(i)require(floats[i*2]>floats[(i-1)*2],"source levels collapsed (RG8/half precision regression)");
    }
    require(words==original,"normalize modified source");
    for(unsigned level=0;level<3;level++)(void)make_mip(std::max(1u,7u>>level),std::max(1u,5u>>level),3,level);
    Mip m=make_mip(7,5,2,9);
    std::vector<uint8_t> unaligned(m.decoded.size()*4+2,0xa7);
    require(xbg_decode_bc5_rg32_linear(m.w,m.h,m.layers,m.blocks.data(),m.blocks.size(),m.row,m.image,
        unaligned.data()+1,unaligned.size()-2)==0,"unaligned output decode failed");
    require(unaligned.front()==0xa7&&unaligned.back()==0xa7,"output canary changed");
    require(std::memcmp(unaligned.data()+1,m.decoded.data(),m.decoded.size()*4)==0,"unaligned result mismatch");
    const size_t bytes=m.decoded.size()*4;
    auto bad=[&](unsigned w,unsigned h,unsigned d,size_t input,size_t row,size_t image,size_t output){
        std::vector<uint8_t> dst(bytes,0x6d);
        require(xbg_decode_bc5_rg32_linear(w,h,d,m.blocks.data(),input,row,image,dst.data(),output)!=0,"invalid decode extent/stride accepted");
        require(std::all_of(dst.begin(),dst.end(),[](uint8_t v){return v==0x6d;}),"invalid call changed output");
    };
    bad(0,5,2,m.blocks.size(),m.row,m.image,bytes);
    bad(7,0,2,m.blocks.size(),m.row,m.image,bytes);
    bad(7,5,0,m.blocks.size(),m.row,m.image,bytes);
    bad(UINT32_MAX,UINT32_MAX,UINT32_MAX,m.blocks.size(),m.row,m.image,bytes);
    bad(7,5,2,m.blocks.size(),16,m.image,bytes);
    bad(7,5,2,m.blocks.size(),m.row+1,m.image,bytes);
    bad(7,5,2,m.blocks.size(),m.row,m.row,bytes);
    bad(7,5,2,m.image+ m.row+32-1,m.row,m.image,bytes);
    bad(7,5,2,m.blocks.size(),m.row,m.image,bytes-1);
    require(xbg_rg16_unorm_to_rg32_float(nullptr,4,floats.data(),8,1)!=0,"null normalize source accepted");
    require(xbg_rg16_unorm_to_rg32_float(words.data(),3,floats.data(),8,1)!=0,"short normalize source accepted");
    require(xbg_rg16_unorm_to_rg32_float(words.data(),4,floats.data(),7,1)!=0,"short normalize output accepted");
    require(xbg_rg16_unorm_to_rg32_float(words.data(),words.size()*2,words.data(),words.size()*2,1)!=0,"overlapping normalize accepted");
    require(xbg_rg16_unorm_to_rg32_float(words.data(),4,floats.data(),8,SIZE_MAX)!=0,"normalize overflow accepted");
    std::printf("bc5_filter_contract CPU PASS normalized_values=65536 channels=2 mips=3 layers=3 non_square=7x5 source_unchanged=1 negative_bounds=1\n");
}

#ifdef __ANDROID__
static void vk_check(VkResult result,const char *call){if(result!=VK_SUCCESS)throw std::runtime_error(std::string(call)+" VkResult="+std::to_string(result));}
#define VK_TEST(call) vk_check((call),#call)
struct HostBuffer {VkBuffer buffer{};VkDeviceMemory memory{};void *mapped{};};
struct Request {float u,v,lod;uint32_t layer,index;};
static double sample_level(const Mip &m,float u,float v,unsigned layer,unsigned channel){
    const double px=double(u)*m.w-.5,py=double(v)*m.h-.5;
    const int x=int(std::floor(px)),y=int(std::floor(py));
    const double fx=px-x,fy=py-y;
    auto at=[&](int ix,int iy){ix=std::max(0,std::min(int(m.w)-1,ix));iy=std::max(0,std::min(int(m.h)-1,iy));
        return double(m.reference[((size_t(layer)*m.h+unsigned(iy))*m.w+unsigned(ix))*2+channel])/65535.0;};
    return (at(x,y)*(1-fx)+at(x+1,y)*fx)*(1-fy)+(at(x,y+1)*(1-fx)+at(x+1,y+1)*fx)*fy;
}
static void gpu_contract(const char *shader_path){
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
    std::printf("bc5_filter_contract old_path=REJECTED_BY_REAL_FORMAT_CAPS vk77_features=%#x new_vk103_features=%#x device=%s\n",old.optimalTilingFeatures,chosen.optimalTilingFeatures,props.deviceName);
    uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,nullptr);std::vector<VkQueueFamilyProperties> families(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,families.data());unsigned family=0;
    while(family<nq&&!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))family++;require(family<nq,"no compute queue");
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
    for(unsigned level=0;level<3;level++)std::memcpy(static_cast<uint8_t*>(staging.mapped)+copies[level].bufferOffset,mips[level].decoded.data(),mips[level].decoded.size()*4);
    std::vector<Request> requests;
    for(unsigned layer=0;layer<2;layer++)for(float lod:{0.f,.5f,1.f,1.5f,2.f})for(unsigned uv=0;uv<4;uv++)
        requests.push_back({uv==0?-.1f:uv==1?.31f:uv==2?.73f:1.1f,uv==0?.22f:uv==1?.67f:uv==2?.41f:.9f,lod,layer,uint32_t(requests.size())});
    HostBuffer results=host_buffer(requests.size()*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);std::memset(results.mapped,0xcd,requests.size()*16);
    VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};im.imageType=VK_IMAGE_TYPE_2D;im.format=VK_FORMAT_R32G32_SFLOAT;im.extent={7,5,1};im.mipLevels=3;im.arrayLayers=2;im.samples=VK_SAMPLE_COUNT_1_BIT;im.tiling=VK_IMAGE_TILING_OPTIMAL;im.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage image;VK_TEST(vkCreateImage(device,&im,nullptr,&image));VkMemoryRequirements ir;vkGetImageMemoryRequirements(device,image,&ir);
    VkMemoryAllocateInfo ia{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ia.allocationSize=ir.size;ia.memoryTypeIndex=memory_type(ir.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory image_memory;VK_TEST(vkAllocateMemory(device,&ia,nullptr,&image_memory));VK_TEST(vkBindImageMemory(device,image,image_memory,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;vi.format=im.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,2};
    VkImageView view;VK_TEST(vkCreateImageView(device,&vi,nullptr,&view));
    xbg_nvn_sampler_desc input{5,1,7,7,7,0,2,0,2,0,1,{0,0,0,0}};
    xbg_sampler_limits limits{VK_FALSE,VK_FALSE,VK_FALSE,1,props.limits.maxSamplerLodBias};VkSamplerCreateInfo si{};
    require(xbg_sampler_from_nvn(&input,&limits,&si)==0,"actual NVN min5/mag1 sampler conversion failed");
    require(si.minFilter==VK_FILTER_LINEAR&&si.magFilter==VK_FILTER_LINEAR&&si.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR,"NVN min5 lost trilinear filtering");
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
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);VK_TEST(vkEndCommandBuffer(command));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;VK_TEST(vkCreateFence(device,&fci,nullptr,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;VK_TEST(vkQueueSubmit(queue,1,&submit,fence));VK_TEST(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
    const float *actual=static_cast<const float*>(results.mapped);double maximum_error=0;
    const double tolerance=2*std::ldexp(1.,-int(props.limits.subTexelPrecisionBits))+std::ldexp(1.,-int(props.limits.mipmapPrecisionBits))+1e-6;
    unsigned rejects_nearest=0,rejects_mip_nearest=0;
    for(const auto &q:requests){unsigned lo=unsigned(std::floor(q.lod)),hi=std::min(2u,lo+1);double blend=q.lod-lo;
        for(unsigned channel=0;channel<4;channel++){
            double expected=channel==2?0:channel==3?1:sample_level(mips[lo],q.u,q.v,q.layer,channel)*(1-blend)+sample_level(mips[hi],q.u,q.v,q.layer,channel)*blend;
            if(channel<2){
                const unsigned nearest_level=unsigned(std::floor(q.lod+.5f));const Mip &m=mips[nearest_level];
                const unsigned x=unsigned(std::max(0,std::min(int(m.w)-1,int(std::floor(q.u*m.w)))));
                const unsigned y=unsigned(std::max(0,std::min(int(m.h)-1,int(std::floor(q.v*m.h)))));
                const double nearest=double(m.reference[((size_t(q.layer)*m.h+y)*m.w+x)*2+channel])/65535.0;
                if(std::fabs(expected-nearest)>4*tolerance)++rejects_nearest;
                if(blend>0&&std::fabs(expected-sample_level(m,q.u,q.v,q.layer,channel))>4*tolerance)++rejects_mip_nearest;
            }
            double error=std::fabs(double(actual[q.index*4+channel])-expected);maximum_error=std::max(maximum_error,error);
            if(!(error<=(channel<2?tolerance:1e-6))){std::fprintf(stderr,"query=%u channel=%u lod=%g actual=%g reference=%.10g error=%g tolerance=%g\n",q.index,channel,q.lod,actual[q.index*4+channel],expected,error,tolerance);throw std::runtime_error("actual GPU linear/mip filtering differs from mathematical reference");}
        }
    }
    require(rejects_nearest>=4&&rejects_mip_nearest>=4,"fixture cannot distinguish forbidden nearest/mip-nearest substitutions");
    VK_TEST(vkDeviceWaitIdle(device));vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDescriptorPool(device,descriptors,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,module,nullptr);vkDestroyPipelineLayout(device,pipeline_layout,nullptr);vkDestroyDescriptorSetLayout(device,layout,nullptr);vkDestroySampler(device,sampler,nullptr);vkDestroyImageView(device,view,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,image_memory,nullptr);
    for(auto b:{staging,results}){vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,nullptr);vkFreeMemory(device,b.memory,nullptr);}vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::printf("bc5_filter_contract GPU PASS samples=%zu min=5 mag=1 bilinear=1 trilinear=1 mips=3 layers=2 B=0 A=1 max_error=%.9g tolerance=%.9g rejects_nearest=%u rejects_mip_nearest=%u full_engine_Image_path=NOT_INVOKED\n",requests.size(),maximum_error,tolerance,rejects_nearest,rejects_mip_nearest);
}
#endif
int main(int argc,char **argv){
    try{
        if(argc==2&&std::strcmp(argv[1],"--shader-source")==0){std::fputs(shader_source,stdout);return 0;}
        cpu_contract();
        if(argc==3&&std::strcmp(argv[1],"--gpu")==0){
#ifdef __ANDROID__
            gpu_contract(argv[2]);
#else
            throw std::runtime_error("GPU component must run on actual Android Vulkan device");
#endif
        }else require(argc==1,"usage: contract [--shader-source | --gpu self-owned.spv]");
        return 0;
    }catch(const std::exception &error){std::fprintf(stderr,"bc5_filter_contract FAIL %s\n",error.what());return 1;}
}
