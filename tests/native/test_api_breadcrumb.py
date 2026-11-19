from pathlib import Path
import tempfile,subprocess,re,collections
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_api_breadcrumb.h');args=p.parse_args();s=args.source.read_text();h=args.header.read_text()
# The scoped OPPO copy-on-write clear records a copy, fill and two buffer barriers.
BASELINE_VULKAN_COUNTS={'vkGetPhysicalDeviceFormatProperties': 12, 'vkFlushMappedMemoryRanges': 2, 'vkInvalidateMappedMemoryRanges': 2, 'vkCreateBuffer': 3, 'vkGetBufferMemoryRequirements': 3, 'vkAllocateMemory': 8, 'vkBindBufferMemory': 3, 'vkMapMemory': 4, 'vkUnmapMemory': 3, 'vkDestroyBuffer': 2, 'vkFreeMemory': 8, 'vkCreateCommandPool': 3, 'vkAllocateCommandBuffers': 3, 'vkCreateFence': 2, 'vkBeginCommandBuffer': 3, 'vkCmdPipelineBarrier': 28, 'vkCmdCopyBuffer': 11, 'vkEndCommandBuffer': 3, 'vkQueueSubmit': 2, 'vkWaitForFences': 3, 'vkDestroyFence': 2, 'vkDestroyCommandPool': 4, 'vkResetCommandPool': 3, 'vkCmdEndQuery': 1, 'vkCmdBeginQuery': 1, 'vkCmdEndRenderPass': 4, 'vkGetFenceStatus': 10, 'vkGetQueryPoolResults': 4, 'vkResetFences': 1, 'vkDestroyFramebuffer': 7, 'vkDestroyBufferView': 6, 'vkCreateQueryPool': 4, 'vkResetCommandBuffer': 4, 'vkCmdResetQueryPool': 6, 'vkCmdWriteTimestamp': 6, 'vkCmdCopyQueryPoolResults': 1, 'vkCmdCopyBufferToImage': 7, 'vkCreateSemaphore': 2, 'vkDestroySemaphore': 4, 'vkGetPipelineCacheData': 2, 'vkDestroyImageView': 5, 'vkDestroyImage': 5, 'vkCreateImageView': 2, 'vkCmdCopyImageToBuffer': 9, 'vkGetPhysicalDeviceImageFormatProperties': 2, 'vkCreateImage': 4, 'vkGetImageMemoryRequirements': 4, 'vkBindImageMemory': 4, 'vkCmdClearColorImage': 2, 'vkCmdClearDepthStencilImage': 2, 'vkDestroyShaderModule': 12, 'vkCreateShaderModule': 12, 'vkGetPhysicalDeviceFormatProperties2': 3, 'vkDestroyPipeline': 16, 'vkCreateDescriptorSetLayout': 5, 'vkCreatePipelineLayout': 7, 'vkDestroyDescriptorSetLayout': 8, 'vkDestroyPipelineLayout': 9, 'vkCreateRenderPass': 6, 'vkDestroyRenderPass': 8, 'vkCreateComputePipelines': 6, 'vkCreateGraphicsPipelines': 3, 'vkDestroyDescriptorUpdateTemplate': 1, 'vkDestroySampler': 3, 'vkCreateDescriptorPool': 2, 'vkAllocateDescriptorSets': 3, 'vkCreateSampler': 3, 'vkUpdateDescriptorSets': 5, 'vkCmdBindPipeline': 7, 'vkCmdBindDescriptorSets': 5, 'vkCmdDispatch': 4, 'vkCmdCopyImage': 3, 'vkCreateBufferView': 2, 'vkCreateDescriptorUpdateTemplate': 2, 'vkUpdateDescriptorSetWithTemplate': 2, 'vkCreateFramebuffer': 3, 'vkCmdBeginRenderPass': 2, 'vkCmdSetViewport': 2, 'vkCmdSetScissor': 2, 'vkCmdBindVertexBuffers': 1, 'vkCmdBindIndexBuffer': 1, 'vkCmdDrawIndexedIndirect': 1, 'vkCmdDrawIndexed': 1, 'vkCmdDraw': 3, 'vkDestroyDescriptorPool': 3, 'vkCmdPushConstants': 4, 'vkCmdDispatchIndirect': 1, 'vkCmdClearAttachments': 4, 'vkCmdBlitImage': 2, 'vkCmdFillBuffer': 1, 'vkCmdResolveImage': 1, 'vkDestroyQueryPool': 4, 'vkGetDeviceProcAddr': 3, 'vkGetPhysicalDeviceProperties': 1, 'vkGetPhysicalDeviceMemoryProperties': 1, 'vkGetPhysicalDeviceProperties2': 1, 'vkGetPhysicalDeviceFeatures': 1, 'vkGetPhysicalDeviceQueueFamilyProperties': 2, 'vkGetPhysicalDeviceFeatures2': 2, 'vkCreatePipelineCache': 2, 'vkEnumerateDeviceExtensionProperties': 4, 'vkQueueWaitIdle': 2, 'vkDestroyPipelineCache': 1}
main=r'''
#include "xbg_api_breadcrumb.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <unistd.h>
#include <sys/stat.h>
static std::vector<std::string> lines(const std::string &path){
 std::ifstream f(path);std::vector<std::string> result;std::string line;
 while(std::getline(f,line))result.push_back(line);return result;
}
static uint64_t value(const std::string &line,const char *key){
 auto at=line.find(key);assert(at!=std::string::npos);return std::stoull(line.substr(at+std::strlen(key)));
}
int main(int argc,char **argv){
 assert(argc==2);std::string path=argv[1];XbgApiBreadcrumb b;
 b.configure(false,path.c_str());assert(b.begin(b.Target,b.RuntimeModule,1)==0);
 b.descriptor_write(b.Target,1,nullptr,0,0);assert(!b.begin_dispatch(b.Target,1,false,0,1,2,3));assert(access(path.c_str(),F_OK));
 b.configure(true,path.c_str());auto initial=lines(path);
 assert(!b.begin("another",b.RuntimeModule,1));assert(!b.begin(nullptr,b.RuntimeModule,1));
 XbgApiBreadcrumb::Descriptor entries[10]{};
 for(unsigned i=0;i<10;++i){entries[i].binding=5+i*6;entries[i].element=i;entries[i].type=8;entries[i].kind=XbgApiBreadcrumb::Descriptor::Buffer;entries[i].offset=16;entries[i].range=32;entries[i].dynamic=true;entries[i].dynamic_offset=256;}
 entries[1].kind=XbgApiBreadcrumb::Descriptor::Image;entries[1].image_metadata=true;
 entries[1].format=97;entries[1].width=128;entries[1].height=64;entries[1].depth=1;entries[1].base_mip=2;entries[1].mip_count=3;entries[1].base_layer=4;entries[1].layer_count=2;
 b.descriptor_write("another",1,entries,10,12);assert(!b.begin_dispatch("another",1,true,512,99,98,97));assert(lines(path)==initial);
 assert(!b.wants_descriptors("another") && b.wants_descriptors(b.Target));
 std::vector<std::thread> threads;
 for(int i=0;i<16;++i)threads.emplace_back([&]{
  b.descriptor_write(b.Target,9,entries,10,12);
  for(unsigned n=0;n<b.Count;++n){auto site=static_cast<XbgApiBreadcrumb::Site>(n);uint32_t w[]={1,2,3};
   auto seq=site==b.Dispatch?b.begin_dispatch(b.Target,9,true,512,99,98,97):b.begin(b.Target,site,9,w,3);b.end(seq,site,10,0);
  }
 });
 for(auto &t:threads)t.join();auto all=lines(path);assert(all.size()==21);
 unsigned starts=0,ends=0,bindings=0,dispatches=0,batches=0;uint64_t previous_ns=0;
 for(const auto &line:all){
  uint64_t ns=value(line,"monotonic_ns=");assert(ns && ns>=previous_ns);previous_ns=ns;
  assert(line.find("0x")==std::string::npos && line.find("address=")==std::string::npos);
  if(line.find("API_BREADCRUMB_SESSION")==0){assert(line.find("max_lines=21 descriptor_limit=8")!=std::string::npos);continue;}
  assert(line.find("tid=0 ")==std::string::npos);
  if(line.find("phase=begin")!=std::string::npos){++starts;if(line.find("site=runtime_module")!=std::string::npos)assert(line.find("fnv64=fd1f0f4381eb0395")!=std::string::npos);}
  if(line.find("phase=end")!=std::string::npos)++ends;
  if(line.find("API_DESCRIPTOR_WRITE")==0){++batches;assert(line.find("total=12 captured=8")!=std::string::npos);}
  if(line.find("API_DESCRIPTOR v=")==0){++bindings;if(line.find("binding=5 ")!=std::string::npos)assert(line.find("offset=16 range=32 dynamic=1 dynamic_offset=256")!=std::string::npos);if(line.find("binding=11 ")!=std::string::npos)assert(line.find("format=97 image_extent=128,64,1 base_mip=2 mip_count=3 base_layer=4 layer_count=2")!=std::string::npos);}
  if(line.find("API_DISPATCH")==0){++dispatches;assert(line.find("gpu_indirect=1 offset=512")!=std::string::npos && line.find("xyz=")==std::string::npos);}
 }
 assert(starts==5 && ends==5 && bindings==8 && dispatches==1 && batches==1 && !b.wants_descriptors(b.Target));
 auto direct=path+".direct";b.configure(true,direct.c_str());auto seq=b.begin_dispatch(b.Target,12,false,999,3,7,11);b.end(seq,b.Dispatch,12,0);
 assert(lines(direct)[2].find("gpu_indirect=0 xyz=3,7,11")!=std::string::npos && lines(direct)[2].find("offset=")==std::string::npos);
 auto incomplete=path+".incomplete";b.configure(true,incomplete.c_str());assert(b.begin(b.Target,b.RuntimePipeline,33));
 auto before=lines(incomplete);assert(before.size()==2 && before[1].find("phase=begin")!=std::string::npos);
 b.configure(true,incomplete.c_str());assert(lines(incomplete+".prev")==before && lines(incomplete).size()==1);
 b.configure(false,path.c_str());assert(!b.begin(b.Target,b.Dispatch,99));
 auto blocked=path+".blocked";{std::ofstream x(blocked);x<<"preserve me\n";}
 assert(mkdir((blocked+".prev").c_str(),0700)==0);b.configure(true,blocked.c_str());assert(!b.begin(b.Target,b.Dispatch,99));assert(lines(blocked)==std::vector<std::string>{"preserve me"});
 auto missing=path+".missing/trace";b.configure(true,missing.c_str());assert(!b.begin(b.Target,b.Dispatch,99));assert(access(missing.c_str(),F_OK));
}
'''

# Execute the production final-view lookup with canonical and filtered objects.
a=s.index('            auto api_image=[&](const Image &image,VkImageView view){')
b=s.index('\n            };',a)+len('\n            };')
image_lookup=s[a:b]
image_test=r'''
#include <array>
#include <map>
#include <memory>
using VkImageView=uint64_t;
struct Image {
 struct {struct {uint32_t width=0,height=0,depth=0;} extent;} create;
 std::map<std::array<uint32_t,9>,VkImageView> views;
 std::unique_ptr<Image> filtered;
};
static void test_image_lookup(){
 bool api_details=true;XbgApiBreadcrumb::Descriptor api_binding;
 IMAGE_LOOKUP
 Image image;image.create.extent={512,256,1};
 image.views[std::array<uint32_t,9>{0,4,0,1,100,2,1,4,0}]=70;
 image.filtered=std::make_unique<Image>();image.filtered->create.extent={128,64,1};
 image.filtered->views[std::array<uint32_t,9>{2,3,4,2,97,5,6,8,0}]=71;
 api_image(image,71);assert(api_binding.image_metadata && api_binding.format==97 && api_binding.width==128 && api_binding.height==64 && api_binding.depth==1);
 assert(api_binding.base_mip==2 && api_binding.mip_count==3 && api_binding.base_layer==4 && api_binding.layer_count==2 && api_binding.view_type==5 && api_binding.aspects==6 && api_binding.usage==8);
 api_binding={};api_image(image,70);assert(api_binding.format==100 && api_binding.width==512);
 api_binding={};api_image(image,999);assert(api_binding.kind==XbgApiBreadcrumb::Descriptor::Image && !api_binding.image_metadata);
 api_details=false;api_binding={};api_image(image,71);assert(api_binding.kind==XbgApiBreadcrumb::Descriptor::Other);
}
'''.replace('IMAGE_LOOKUP',image_lookup)
main=main.replace('int main(int argc,char **argv){',image_test+'\nint main(int argc,char **argv){\n test_image_lookup();')

with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'xbg_api_breadcrumb.h').write_text(h);(p/'test.cpp').write_text(main)
 subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test'),str(p/'log')],check=True)
def vk_calls(src):
 src=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"','',src,flags=re.S)
 return collections.Counter(re.findall(r'\b(vk\w+)\s*\(',src))
assert vk_calls(s)==BASELINE_VULKAN_COUNTS,'Vulkan call count changed'
assert 'gfx-api-breadcrumb' not in s
assert 'const bool on=FFRS_API_BREADCRUMB_DIAGNOSTIC != 0;' in s
assert 'api_breadcrumb.configure(on,(base+"/api-breadcrumbs.log").c_str());' in s
assert 'FFRS_PROFILE_TABLET' not in s[s.index('const bool on=FFRS_API_BREADCRUMB_DIAGNOSTIC'):s.index('api_breadcrumb.configure(on,')]
assert 'check(api_result,"vkCreateShaderModule(cfg.device,&ci,nullptr,&p->module)")' in s
assert 'create_compute_pipeline_resilient(cfg.device,pipeline_cache,1,&cc,nullptr,&out.pipeline)' in s
assert 'warm::register_module(p->module,key)' in s
assert 'vkCmdDispatch(c.command,p.x,p.y,p.z);\n    api_breadcrumb.end' in s
assert 'return enabled && key && std::strcmp(key,Target)==0;' in h and 'seen.fetch_or' in h and h.count('fflush(file)')==5
# True negative: missing flush must fail source contract, not silently pass.
for token in ['seen.fetch_or','fflush(file)']:
 corrupt=h.replace(token,'REMOVED',1)
 assert not ('seen.fetch_or' in corrupt and corrupt.count('fflush(file)')==5)
assert 'api_breadcrumb.begin_dispatch(state.shader[5]->key.c_str(),stats.presents.load(),gpu_indirect,indirect.offset,uint32_t(p.x),uint32_t(p.y),uint32_t(p.z))' in s
block=s[s.index('void descriptors_build(Descriptors &out,Commands &commands,bool compute){'):s.index('// Render passes are cached')]
assert block.count('api_descriptor_write();')==4
assert block.count('vkUpdateDescriptorSetWithTemplate(cfg.device,out.set,e.tmpl,data.data());api_descriptor_write();')==2
assert block.count('vkUpdateDescriptorSets(cfg.device,uint32_t(writes.size()),writes.data(),0,nullptr);api_descriptor_write();')==2
assert block.count('api_image(')==2
assert 'api_binding.offset=write.pBufferInfo->offset;api_binding.range=write.pBufferInfo->range;' in block
assert 'api_binding.dynamic_offset=dynamic_list.back().second;' in block
assert 'if(hit!=slot.set_cache.end()){out.set=hit->second;++set_cache_hits;return;}' in block
assert 'FFRS_API_BREADCRUMB_DIAGNOSTIC && compute' in block
print('PASS ASan/UBSan disabled/wrong-key,16-thread bounded21lines,monotonic,FNV,descriptor truncation,direct/indirect,rotation/failure,unchanged Vulkan calls and final-object source wiring; device NOT_RUN')
