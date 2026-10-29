/* Self-owned shader/data. CPU: production feature/driver/cache/view guards.
 * Android GPU: production backend creates/enables the device; production stage
 * and layered-framebuffer planners feed an actual graphics pipeline. This
 * component does not invoke the full guest engine/descriptor memo path. */
#include "../../src/graphics/nvn_vulkan/engine/xbg_game.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_framebuffer.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_stages.h"
extern "C" {
#include "../../src/graphics/nvn_device/nvn_vulkan_features.h"
}
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static const char *vertex_source=R"glsl(#version 460
#extension GL_ARB_shader_viewport_layer_array : require
layout(set=0,binding=0,std140) uniform Driver { layout(offset=1604) int base_instance; } driver;
layout(location=1) flat out int original_bits;
void main(){
    vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    int layer=(gl_InstanceIndex-gl_BaseInstance)+driver.base_instance;
    gl_Layer=layer;original_bits=layer;
    gl_Position=vec4(p[gl_VertexIndex],0,1);
}
)glsl";
static const char *fragment_source=R"glsl(#version 450
layout(location=1) flat in int original_bits;
layout(location=0) out vec4 color;
void main(){color=vec4(float(original_bits)/8.0,0,0,1);}
)glsl";
static void require(bool p,const char *m){if(!p)throw std::runtime_error(m);}
static void cpu_contract(){
    const uint32_t v12=VK_API_VERSION_1_2,v13=VK_API_VERSION_1_3;
    require(nvn_vulkan_layer_output_route(v12,v13,1,1),"valid promoted feature route refused");
    require(!nvn_vulkan_layer_output_route(VK_API_VERSION_1_1,v13,1,1),"physical API upgraded an old requested instance");
    require(!nvn_vulkan_layer_output_route(v13,VK_API_VERSION_1_1,1,1),"old physical API accepted Vulkan12 route");
    require(!nvn_vulkan_layer_output_route(v13,v13,0,1)&&!nvn_vulkan_layer_output_route(v13,v13,1,0),"unsupported output pair accepted");
    std::vector<uint32_t> module{0x07230203,0x00010500,0,1,0,(2u<<16)|17,69};
    auto check=[&](VkBool32 layer,VkBool32 viewport,uint32_t instance,uint32_t physical,uint32_t stage=0){return xbg_shader_layer_check(module.data(),module.size(),stage,instance,physical,layer,viewport);};
    require(check(1,0,v12,v13)==0,"core Layer incorrectly requires multiViewport or legacy extension");
    require(check(0,1,v12,v13)!=0,"supported but disabled Layer accepted");
    require(check(1,1,VK_API_VERSION_1_1,v13)!=0&&check(1,1,v13,VK_API_VERSION_1_1)!=0,"invalid API route accepted");
    require(check(1,1,v13,v13,4)!=0,"unreviewed output stage accepted");
    module[6]=70;require(check(1,0,v13,v13)!=0&&check(0,1,v13,v13)==0,"ViewportIndex feature gate aliased Layer");
    module[6]=5254;require(check(1,1,v13,v13)!=0,"legacy combined capability accepted without extension route");
    module[6]=69;module[1]=0x00010300;require(check(1,1,v13,v13)!=0,"core Layer accepted without SPIR-V1.5 contract");
    module[1]=0x00010500;module.push_back((2u<<16)|10);module.push_back(0x00584146);require(check(1,1,v13,v13)!=0,"unknown extension accepted");
    std::array<unsigned char,0x1000> block;block.fill(0x5e);const auto original=block;
    require(xbg_driver_base_instance_write(block.data(),0x647,2)!=0&&block==original,"short c0 write changed memory");
    require(xbg_driver_base_instance_write(nullptr,block.size(),2)!=0,"null c0 accepted");
    require(xbg_driver_base_instance_write(block.data(),block.size(),0xa1b2c3d4)==0,"valid c0 publication refused");
    uint32_t actual;std::memcpy(&actual,block.data()+0x644,4);require(actual==0xa1b2c3d4,"base instance published at wrong byte offset");
    for(size_t i=0;i<block.size();i++)if(i<0x644||i>=0x648)require(block[i]==0x5e,"base instance changed another c0 word");
    xbg_driver_cache_key stamp{7,19,2};
    require(xbg_driver_cache_matches(&stamp,7,19,2),"identical driver key missed");
    require(!xbg_driver_cache_matches(&stamp,7,19,4),"changed base reused a driver block without any buffer rebind");
    require(!xbg_driver_cache_matches(&stamp,8,19,2)&&!xbg_driver_cache_matches(&stamp,7,20,2)&&!xbg_driver_cache_matches(&stamp,0,19,2),"slot/generation/zero key invalidation failed");
    xbg_framebuffer_attachment a{1,16,16,1,6,0,1,0,6,1};xbg_framebuffer_extent extent{};uint32_t bad;
    require(xbg_framebuffer_plan_layered(&a,1,64,64,&extent,&bad)==0&&extent.layers==6,"production layered planner rejected valid view");
    a.base_layer=5;a.layer_count=1;require(xbg_framebuffer_plan_layered(&a,1,64,64,&extent,&bad)==0&&extent.layers==1,"one-layer subview plan failed");
    const auto saved=extent;a.layer_count=2;require(xbg_framebuffer_plan_layered(&a,1,64,64,&extent,&bad)!=0&&std::memcmp(&saved,&extent,sizeof extent)==0,"out-of-range view accepted/changed output");
    auto range=[](uint32_t first,uint32_t instances,uint32_t layers){return instances&&first<layers&&instances<=layers-first;};
    require(range(2,1,6)&&!range(2,1,1)&&!range(UINT32_MAX,2,6),"self-owned Layer formula range preflight failed");
    std::puts("layer_output CPU PASS supported_vs_enabled=1 API_route=1 legacy_unknown_extension_refused=1 c0_644_canaries=1 cache_base_change_no_rebind=1 one_multi_views=1 out_of_range=1");
}
#ifdef __ANDROID__
static void vk_ok(VkResult r,const char *m){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(m)+" VkResult="+std::to_string(r));}
#define VKC(x) vk_ok(x,#x)
static std::vector<uint32_t> read_spv(const char *path){std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"shader open failed");auto size=file.tellg();require(size>=20&&size%4==0,"shader size invalid");std::vector<uint32_t>w(size_t(size)/4);file.seekg(0);file.read(reinterpret_cast<char*>(w.data()),size);require(bool(file),"shader read failed");return w;}
static void gpu_contract(const char *vert_path,const char *frag_path){
    nvn_device_state state{};state.backend=nvn_device_default_backend();require(state.backend&&state.backend->create(&state,nullptr),"production Vulkan backend creation failed");
    nvn_vulkan_enabled_features enabled{};require(nvn_vulkan_get_enabled_features(&state,&enabled),"production enabled-feature getter refused its own device");
    require(enabled.shader_output_layer&&enabled.shader_output_viewport_index&&enabled.shader_draw_parameters,"required actual logical-device output/draw features were not enabled");
    void *p,*d,*q;uint32_t family;require(state.backend->get_device_queue(&state,&p,&d,&q,&family),"production device handles unavailable");
    VkPhysicalDevice physical=(VkPhysicalDevice)p;VkDevice device=(VkDevice)d;VkQueue queue=(VkQueue)q;
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);
    auto vert=read_spv(vert_path),frag=read_spv(frag_path);
    require(xbg_shader_layer_check(vert.data(),vert.size(),0,enabled.instance_api_version,enabled.physical_api_version,enabled.shader_output_layer,enabled.shader_output_viewport_index)==0,"production core Layer shader policy rejected enabled device");
    require(xbg_shader_layer_check(vert.data(),vert.size(),0,enabled.instance_api_version,enabled.physical_api_version,0,enabled.shader_output_viewport_index)!=0,"disabled policy did not refuse before module creation");
    VkPhysicalDeviceMemoryProperties memories;vkGetPhysicalDeviceMemoryProperties(physical,&memories);
    auto mt=[&](uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memories.memoryTypeCount;i++)if((bits&(1u<<i))&&(memories.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("required memory type unavailable");};
    struct Buffer{VkBuffer buffer;VkDeviceMemory memory;void *mapped;};
    auto buffer=[&](size_t bytes,VkBufferUsageFlags usage){Buffer b{};VkBufferCreateInfo c{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.size=bytes;c.usage=usage;VKC(vkCreateBuffer(device,&c,nullptr,&b.buffer));VkMemoryRequirements r;vkGetBufferMemoryRequirements(device,b.buffer,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=mt(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VKC(vkAllocateMemory(device,&a,nullptr,&b.memory));VKC(vkBindBufferMemory(device,b.buffer,b.memory,0));VKC(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped));return b;};
    Buffer driver=buffer(0x1000,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),pixels=buffer(16*16*6*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);std::memset(driver.mapped,0,0x1000);
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image_info.imageType=VK_IMAGE_TYPE_2D;image_info.format=VK_FORMAT_R8G8B8A8_UNORM;image_info.extent={16,16,1};image_info.mipLevels=1;image_info.arrayLayers=6;image_info.samples=VK_SAMPLE_COUNT_1_BIT;image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImage image;VKC(vkCreateImage(device,&image_info,nullptr,&image));VkMemoryRequirements ir;vkGetImageMemoryRequirements(device,image,&ir);VkMemoryAllocateInfo ia{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ia.allocationSize=ir.size;ia.memoryTypeIndex=mt(ir.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);VkDeviceMemory image_memory;VKC(vkAllocateMemory(device,&ia,nullptr,&image_memory));VKC(vkBindImageMemory(device,image,image_memory,0));
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr};VkDescriptorSetLayoutCreateInfo slc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};slc.bindingCount=1;slc.pBindings=&binding;VkDescriptorSetLayout set_layout;VKC(vkCreateDescriptorSetLayout(device,&slc,nullptr,&set_layout));VkPipelineLayoutCreateInfo plc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};plc.setLayoutCount=1;plc.pSetLayouts=&set_layout;VkPipelineLayout layout;VKC(vkCreatePipelineLayout(device,&plc,nullptr,&layout));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};VkDescriptorPoolCreateInfo pc{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pc.maxSets=1;pc.poolSizeCount=1;pc.pPoolSizes=&ps;VkDescriptorPool descriptor_pool;VKC(vkCreateDescriptorPool(device,&pc,nullptr,&descriptor_pool));VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=descriptor_pool;sa.descriptorSetCount=1;sa.pSetLayouts=&set_layout;VkDescriptorSet set;VKC(vkAllocateDescriptorSets(device,&sa,&set));VkDescriptorBufferInfo bi{driver.buffer,0,0x1000};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&bi;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
    VkAttachmentDescription attachment{};attachment.format=image_info.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;VkRenderPassCreateInfo rpc{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpc.attachmentCount=1;rpc.pAttachments=&attachment;rpc.subpassCount=1;rpc.pSubpasses=&sub;VkRenderPass render_pass;VKC(vkCreateRenderPass(device,&rpc,nullptr,&render_pass));
    auto module=[&](const std::vector<uint32_t>&words){VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};m.codeSize=words.size()*4;m.pCode=words.data();VkShaderModule result;VKC(vkCreateShaderModule(device,&m,nullptr,&result));return result;};
    VkShaderModule vs=module(vert),fs=module(frag),modules[5]{vs,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,fs};VkPipelineShaderStageCreateInfo stages[5]{};uint32_t nstages=0;require(xbg_graphics_stages(modules,0,1,15,0,stages,&nstages)==0,"production graphics-stage planner failed");
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;VkViewport viewport{0,0,16,16,0,1};VkRect2D scissor{{0,0},{16,16}};VkPipelineViewportStateCreateInfo viewport_state{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport_state.viewportCount=viewport_state.scissorCount=1;viewport_state.pViewports=&viewport;viewport_state.pScissors=&scissor;VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend_state.attachmentCount=1;blend_state.pAttachments=&blend;VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=nstages;pi.pStages=stages;pi.pVertexInputState=&vi;pi.pInputAssemblyState=&assembly;pi.pViewportState=&viewport_state;pi.pRasterizationState=&raster;pi.pMultisampleState=&ms;pi.pColorBlendState=&blend_state;pi.layout=layout;pi.renderPass=render_pass;VkPipeline pipeline;VKC(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline));
    VkCommandPoolCreateInfo cpc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpc.queueFamilyIndex=family;cpc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool command_pool;VKC(vkCreateCommandPool(device,&cpc,nullptr,&command_pool));VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=command_pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VkCommandBuffer command;VKC(vkAllocateCommandBuffers(device,&ca,&command));VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;VKC(vkCreateFence(device,&fc,nullptr,&fence));
    auto begin=[&](){VKC(vkResetCommandBuffer(command,0));VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VKC(vkBeginCommandBuffer(command,&b));};
    auto finish=[&](){VKC(vkEndCommandBuffer(command));VKC(vkResetFences(device,1,&fence));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;VKC(vkQueueSubmit(queue,1,&submit,fence));VKC(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));};
    VkImageLayout image_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    auto transition=[&](VkImageLayout next){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=image_layout;b.newLayout=next;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};b.srcAccessMask=image_layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);image_layout=next;};
    auto clear=[&](){begin();transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue zero{};VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};vkCmdClearColorImage(command,image,image_layout,&zero,1,&all);transition(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);finish();};
    std::vector<VkImageView> views;std::vector<VkFramebuffer> framebuffers;
    auto framebuffer=[&](uint32_t base,uint32_t layers){xbg_framebuffer_attachment a{1,16,16,1,6,0,1,base,layers,1};xbg_framebuffer_extent extent{};uint32_t bad;require(xbg_framebuffer_plan_layered(&a,1,props.limits.maxFramebufferWidth,props.limits.maxFramebufferHeight,&extent,&bad)==0,"production view range preflight failed");VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vc.image=image;vc.viewType=layers==1?VK_IMAGE_VIEW_TYPE_2D:VK_IMAGE_VIEW_TYPE_2D_ARRAY;vc.format=image_info.format;vc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,base,layers};VkImageView view;VKC(vkCreateImageView(device,&vc,nullptr,&view));views.push_back(view);VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};f.renderPass=render_pass;f.attachmentCount=1;f.pAttachments=&view;f.width=extent.width;f.height=extent.height;f.layers=extent.layers;VkFramebuffer fb;VKC(vkCreateFramebuffer(device,&f,nullptr,&fb));framebuffers.push_back(fb);return fb;};
    xbg_driver_cache_key stamp{};unsigned driver_refreshes=0;
    auto draw=[&](VkFramebuffer fb,uint32_t view_layers,uint32_t base){require(base<view_layers,"self-owned shader Layer output exceeds selected framebuffer view");if(!xbg_driver_cache_matches(&stamp,1,7,base)){require(xbg_driver_base_instance_write(driver.mapped,0x1000,base)==0,"production c0 helper failed");stamp={1,7,base};driver_refreshes++;}begin();transition(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};rp.renderPass=render_pass;rp.framebuffer=fb;rp.renderArea=scissor;vkCmdBeginRenderPass(command,&rp,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);vkCmdDraw(command,3,1,0,base);vkCmdEndRenderPass(command);finish();};
    auto read=[&](){begin();transition(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,6};copy.imageExtent={16,16,1};vkCmdCopyImageToBuffer(command,image,image_layout,pixels.buffer,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);finish();};
    auto expect=[&](const std::array<int,6>&expected){const auto *data=(const unsigned char*)pixels.mapped;for(unsigned l=0;l<6;l++)for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){const auto *pixel=data+((l*16+y)*16+x)*4;require(pixel[1]==0&&pixel[2]==0,"location1 or channels changed unexpectedly");if(expected[l]<0)require(pixel[0]==0&&pixel[3]==0,"untargeted layer was modified");else require(std::abs(int(pixel[0])-expected[l])<=1&&pixel[3]==255,"Layer/location1 output pixel mismatch");}};
    VkFramebuffer multi=framebuffer(0,6);clear();draw(multi,6,2);draw(multi,6,4);read();expect({-1,-1,64,-1,128,-1});require(driver_refreshes==2,"baseInstance change without buffer binding change did not refresh driver data");
    VkFramebuffer one=framebuffer(5,1);clear();draw(one,1,0);read();expect({-1,-1,-1,-1,-1,0});
    VkFramebuffer offset=framebuffer(1,4);clear();draw(offset,4,2);read();expect({-1,-1,-1,64,-1,-1});
    bool refused=false;try{draw(one,1,2);}catch(const std::runtime_error&){refused=true;}require(refused,"out-of-range Layer reached command recording");
    VKC(vkDeviceWaitIdle(device));for(auto f:framebuffers)vkDestroyFramebuffer(device,f,nullptr);for(auto v:views)vkDestroyImageView(device,v,nullptr);vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,command_pool,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyShaderModule(device,vs,nullptr);vkDestroyShaderModule(device,fs,nullptr);vkDestroyRenderPass(device,render_pass,nullptr);vkDestroyDescriptorPool(device,descriptor_pool,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyDescriptorSetLayout(device,set_layout,nullptr);vkDestroyImage(device,image,nullptr);vkFreeMemory(device,image_memory,nullptr);for(auto b:{driver,pixels}){vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.buffer,nullptr);vkFreeMemory(device,b.memory,nullptr);}state.backend->destroy(&state);
    nvn_vulkan_enabled_features after{};require(!nvn_vulkan_get_enabled_features(&state,&after),"destroyed backend leaked enabled-feature record");
    std::printf("layer_output GPU PASS device=%s requestedInstance=%u.%u physicalAPI=%u.%u multiViewport=%u enabledLayer=%u enabledViewport=%u nonzeroBase2and4=1 sameBufferBindings=1 actualLayerPixels=4608 oneLayerView=1 baseLayerSubview=1 negativeOutOfRange=1 originalLocation1=1 full_engine_descriptor_memo=NOT_INVOKED\n",props.deviceName,VK_VERSION_MAJOR(enabled.instance_api_version),VK_VERSION_MINOR(enabled.instance_api_version),VK_VERSION_MAJOR(enabled.physical_api_version),VK_VERSION_MINOR(enabled.physical_api_version),enabled.core.multiViewport,enabled.shader_output_layer,enabled.shader_output_viewport_index);
}
#endif
int main(int argc,char**argv){try{if(argc==3&&!std::strcmp(argv[1],"--shader-source")){std::fputs(!std::strcmp(argv[2],"vert")?vertex_source:fragment_source,stdout);return 0;}cpu_contract();if(argc==4&&!std::strcmp(argv[1],"--gpu")){
#ifdef __ANDROID__
    gpu_contract(argv[2],argv[3]);
#else
    throw std::runtime_error("GPU contract requires actual Android Vulkan backend");
#endif
}else require(argc==1,"usage: contract [--shader-source vert|frag | --gpu vert.spv frag.spv]");return 0;}catch(const std::exception&e){std::fprintf(stderr,"layer_output FAIL %s\n",e.what());return 1;}}
