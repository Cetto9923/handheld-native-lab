from pathlib import Path
import tempfile,subprocess,re,collections
import argparse
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp');p.add_argument('--header',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_api_breadcrumb.h');args=p.parse_args();s=args.source.read_text();h=args.header.read_text()
BASELINE_VULKAN_COUNTS={'vkGetPhysicalDeviceFormatProperties': 12, 'vkFlushMappedMemoryRanges': 2, 'vkInvalidateMappedMemoryRanges': 2, 'vkCreateBuffer': 3, 'vkGetBufferMemoryRequirements': 3, 'vkAllocateMemory': 8, 'vkBindBufferMemory': 3, 'vkMapMemory': 4, 'vkUnmapMemory': 3, 'vkDestroyBuffer': 2, 'vkFreeMemory': 8, 'vkCreateCommandPool': 3, 'vkAllocateCommandBuffers': 3, 'vkCreateFence': 2, 'vkBeginCommandBuffer': 3, 'vkCmdPipelineBarrier': 28, 'vkCmdCopyBuffer': 10, 'vkEndCommandBuffer': 3, 'vkQueueSubmit': 2, 'vkWaitForFences': 3, 'vkDestroyFence': 2, 'vkDestroyCommandPool': 4, 'vkResetCommandPool': 3, 'vkCmdEndQuery': 1, 'vkCmdBeginQuery': 1, 'vkCmdEndRenderPass': 4, 'vkGetFenceStatus': 10, 'vkGetQueryPoolResults': 4, 'vkResetFences': 1, 'vkDestroyFramebuffer': 7, 'vkDestroyBufferView': 6, 'vkCreateQueryPool': 4, 'vkResetCommandBuffer': 4, 'vkCmdResetQueryPool': 6, 'vkCmdWriteTimestamp': 6, 'vkCmdCopyQueryPoolResults': 1, 'vkCmdCopyBufferToImage': 7, 'vkCreateSemaphore': 2, 'vkDestroySemaphore': 4, 'vkGetPipelineCacheData': 2, 'vkDestroyImageView': 5, 'vkDestroyImage': 5, 'vkCreateImageView': 2, 'vkCmdCopyImageToBuffer': 9, 'vkGetPhysicalDeviceImageFormatProperties': 2, 'vkCreateImage': 4, 'vkGetImageMemoryRequirements': 4, 'vkBindImageMemory': 4, 'vkCmdClearColorImage': 2, 'vkCmdClearDepthStencilImage': 2, 'vkDestroyShaderModule': 12, 'vkCreateShaderModule': 12, 'vkGetPhysicalDeviceFormatProperties2': 3, 'vkDestroyPipeline': 16, 'vkCreateDescriptorSetLayout': 5, 'vkCreatePipelineLayout': 7, 'vkDestroyDescriptorSetLayout': 8, 'vkDestroyPipelineLayout': 9, 'vkCreateRenderPass': 6, 'vkDestroyRenderPass': 8, 'vkCreateComputePipelines': 6, 'vkCreateGraphicsPipelines': 3, 'vkDestroyDescriptorUpdateTemplate': 1, 'vkDestroySampler': 3, 'vkCreateDescriptorPool': 2, 'vkAllocateDescriptorSets': 3, 'vkCreateSampler': 3, 'vkUpdateDescriptorSets': 5, 'vkCmdBindPipeline': 7, 'vkCmdBindDescriptorSets': 5, 'vkCmdDispatch': 4, 'vkCmdCopyImage': 3, 'vkCreateBufferView': 2, 'vkCreateDescriptorUpdateTemplate': 2, 'vkUpdateDescriptorSetWithTemplate': 2, 'vkCreateFramebuffer': 3, 'vkCmdBeginRenderPass': 2, 'vkCmdSetViewport': 2, 'vkCmdSetScissor': 2, 'vkCmdBindVertexBuffers': 1, 'vkCmdBindIndexBuffer': 1, 'vkCmdDrawIndexedIndirect': 1, 'vkCmdDrawIndexed': 1, 'vkCmdDraw': 3, 'vkDestroyDescriptorPool': 3, 'vkCmdPushConstants': 4, 'vkCmdDispatchIndirect': 1, 'vkCmdClearAttachments': 4, 'vkCmdBlitImage': 2, 'vkCmdFillBuffer': 2, 'vkCmdResolveImage': 1, 'vkDestroyQueryPool': 4, 'vkGetDeviceProcAddr': 3, 'vkGetPhysicalDeviceProperties': 1, 'vkGetPhysicalDeviceMemoryProperties': 1, 'vkGetPhysicalDeviceProperties2': 1, 'vkGetPhysicalDeviceFeatures': 1, 'vkGetPhysicalDeviceQueueFamilyProperties': 2, 'vkGetPhysicalDeviceFeatures2': 2, 'vkCreatePipelineCache': 1, 'vkEnumerateDeviceExtensionProperties': 4, 'vkQueueWaitIdle': 2, 'vkDestroyPipelineCache': 1}
main=r'''
#include "xbg_api_breadcrumb.h"
#include <cassert>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <unistd.h>
int main(int argc,char **argv){assert(argc==2);std::string path=argv[1];XbgApiBreadcrumb b;
 b.configure(false,path.c_str());assert(b.begin(b.Target,b.RuntimeModule,1)==0);assert(access(path.c_str(),F_OK));
 b.configure(true,path.c_str());assert(!b.begin("another",b.RuntimeModule,1));assert(!b.begin(nullptr,b.RuntimeModule,1));
 std::vector<std::thread> threads;
 for(int i=0;i<16;++i)threads.emplace_back([&]{for(unsigned n=0;n<b.Count;++n){auto site=static_cast<XbgApiBreadcrumb::Site>(n);uint32_t w[]={1,2,3};auto seq=b.begin(b.Target,site,9,w,3);b.end(seq,site,10,0);}});
 for(auto &t:threads)t.join();std::ifstream f(path);std::string line;unsigned count=0;std::set<std::string> seqs;
 while(std::getline(f,line)){++count;if(count==1){assert(line.find("API_BREADCRUMB_SESSION v=1 pid=")==0 && line.find("init_monotonic_ns=")!=std::string::npos);continue;}assert(line.find("shader=073ce354a4f65e2f")!=std::string::npos && line.find("tid=0 ")==std::string::npos);seqs.insert(line.substr(line.find("seq="),5));}
 assert(count==1+2*b.Count && seqs.size()==b.Count);
 auto incomplete=path+".incomplete";b.configure(true,incomplete.c_str());assert(b.begin(b.Target,b.RuntimePipeline,33));
 // File is visible before end/configure/destructor: fflush is part of begin.
 std::ifstream q(incomplete);assert(std::getline(q,line));assert(line.find("API_BREADCRUMB_SESSION")==0);assert(std::getline(q,line));assert(line.find("phase=begin")!=std::string::npos);assert(!std::getline(q,line));
 b.configure(false,path.c_str());assert(!b.begin(b.Target,b.Dispatch,99));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'xbg_api_breadcrumb.h').write_text(h);(p/'test.cpp').write_text(main)
 subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test'),str(p/'log')],check=True)
def vk_calls(src):
 src=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"','',src,flags=re.S)
 return collections.Counter(re.findall(r'\b(vk\w+)\s*\(',src))
assert vk_calls(s)==BASELINE_VULKAN_COUNTS,'Vulkan call count changed'
assert s.count('gfx-api-breadcrumb')==1
assert 'api_breadcrumb.configure(on,(base+"/api-breadcrumbs.log").c_str());' in s
assert 'FFRS_PROFILE_TABLET' not in s[s.index('bool on=false;FILE *m=fopen((base+"/gfx-api-breadcrumb")'):s.index('api_breadcrumb.configure(on,')]
assert 'check(api_result,"vkCreateShaderModule(cfg.device,&ci,nullptr,&p->module)")' in s
assert 'create_compute_pipeline_resilient(cfg.device,pipeline_cache,1,&cc,nullptr,&out.pipeline)' in s
assert 'warm::register_module(p->module,key)' in s
assert 'vkCmdDispatch(c.command,p.x,p.y,p.z);\n    api_breadcrumb.end' in s
assert 'if(!enabled || !key' in h and 'seen.fetch_or' in h and h.count('fflush(file)')==3
# True negative: missing flush must fail source contract, not silently pass.
for token in ['seen.fetch_or','fflush(file)']:
 corrupt=h.replace(token,'REMOVED',1)
 assert not ('seen.fetch_or' in corrupt and corrupt.count('fflush(file)')==3)
print('PASS ASan/UBSan disabled/wrong-key,16-thread bounded11lines,begin immediately readable, unchanged Vulkan count/return checks; device NOT_RUN')
