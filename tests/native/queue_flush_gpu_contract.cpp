/* Actual-engine Android GPU contract, without game assets or a window.
 *
 * xbg_submit records a real vkCmdClearColorImage and fences that submission.
 * With gfx-legacy-waitall=0, zero-timeout polls must remain incomplete before
 * Flush, then make progress after Flush. Only AFTER that proof may ReadTexels
 * synchronize its own GPU readback. The CPU texture bytes stay poisoned, so a
 * CPU-only clear/readback fallback cannot satisfy the pixel assertions.
 * Shutdown may wait for teardown; no shutdown/readback occurs in the proof.
 *
 * Link against the actual libffrs_nvn_vulkan_engine.a, Vulkan, log and dl.
 * Usage: queue_flush_gpu_contract.android [private-temporary-parent-directory]
 */
#include "../../src/graphics/nvn_vulkan/engine/xbg_game_internal.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {
[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr,"queue_flush_gpu_contract FAIL: %s\n",message);
    // Avoid teardown submitting/waiting and obscuring a failed progress proof.
    std::fflush(nullptr);
    std::_Exit(1);
}
void check(bool condition,const char *message) { if(!condition)fail(message); }
void vk_check(VkResult result,const char *expression,int line) {
    if(result!=VK_SUCCESS){
        std::fprintf(stderr,"Vulkan line=%d expression=%s result=%d\n",line,expression,int(result));
        fail("Vulkan call failed");
    }
}
#define VK_CHECK(expression) vk_check((expression),#expression,__LINE__)
bool saw_poll_mode=false,saw_no_step=false,saw_synchronous_translation=false;
void engine_log(void *,const char *message) {
    std::fprintf(stderr,"[actual-xbg] %s\n",message);
    if(std::strstr(message,"legacy_waitall=0"))saw_poll_mode=true;
    if(std::strstr(message,"CODEX_GPU step diagnosis=0"))saw_no_step=true;
    if(std::strstr(message,"NVN async translation=0"))saw_synchronous_translation=true;
}
void engine_fatal(void *,const char *message) { fail(message); }
int unexpected_present(void *,void *,VkImage,VkFormat,uint32_t,uint32_t,const int32_t[4],int32_t) {
    fail("unexpected presentation: this contract has no window");
}
void marker(const std::string &directory,const char *name,const char *value) {
    const std::string path=directory+"/"+name;
    FILE *file=std::fopen(path.c_str(),"wx");
    check(file!=nullptr,"cannot exclusively create private test marker");
    const size_t n=std::strlen(value);
    const bool written=std::fwrite(value,1,n,file)==n;
    const int closed=std::fclose(file);
    check(written&&closed==0,"cannot save private test marker");
}
uint64_t monotonic_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Stream {
    alignas(8) uint8_t bytes[512]{};
    size_t size=0;
    void append(uint32_t opcode,const void *payload,size_t payload_bytes) {
        const size_t record_bytes=(sizeof(op_hdr)+payload_bytes+7)&~size_t(7);
        check(size+record_bytes<=sizeof bytes,"test command stream overflow");
        const op_hdr header{opcode,uint32_t(record_bytes)};
        std::memcpy(bytes+size,&header,sizeof header);
        if(payload_bytes)std::memcpy(bytes+size+sizeof header,payload,payload_bytes);
        size+=record_bytes;
    }
};
}

int main(int argc,char **argv) {
    check(argc<=2,"usage: executable [private-temporary-parent-directory]");
    std::string pattern=std::string(argc==2?argv[1]:"/data/local/tmp")+"/ffrs-qflush-XXXXXX";
    std::vector<char> temporary(pattern.begin(),pattern.end());temporary.push_back('\0');
    char *created=mkdtemp(temporary.data());
    if(!created){std::fprintf(stderr,"mkdtemp errno=%d\n",errno);fail("cannot create private test directory");}
    const std::string directory=created,shader_directory=directory+"/shaders";
    check(mkdir(shader_directory.c_str(),0700)==0,"cannot create empty private shader directory");
    marker(directory,"gfx-legacy-waitall","0\n");
    marker(directory,"gfx-flush-submit","1\n");
    marker(directory,"gfx-codex-gpu-step","0\n");
    marker(directory,"gfx-sync-mode","0\n");
    marker(directory,"gfx-sync-submit","0\n");
    marker(directory,"gfx-async-pool-reset","0\n");
    marker(directory,"gfx-no-warm","1\n");
    marker(directory,"gfx-perf-sample","0\n");
    std::printf("queue_flush_gpu_contract private_directory=%s assets=NONE\n",directory.c_str());
    std::fflush(stdout);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName="FFRS actual-engine QueueFlush contract";
    app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo=&app;
    VkInstance instance=VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instance_info,nullptr,&instance));
    uint32_t count=0;VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr));
    check(count>0,"no Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    uint32_t family=UINT32_MAX;
    for(auto candidate:devices){
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(candidate,&properties);
        if(properties.apiVersion<VK_API_VERSION_1_1)continue;
        uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
        std::vector<VkQueueFamilyProperties> queues(n);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,queues.data());
        for(uint32_t i=0;i<n;i++)if(queues[i].queueCount&&
            (queues[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==
                (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){
            physical=candidate;family=i;
            std::printf("device=%s vendor=%#x driver=%#x api=%u.%u.%u family=%u\n",
                properties.deviceName,properties.vendorID,properties.driverVersion,
                VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion),family);
            break;
        }
        if(physical)break;
    }
    check(physical!=VK_NULL_HANDLE,"no Vulkan1.1 graphics+compute queue");
    VkPhysicalDeviceShaderDrawParametersFeatures draw_parameters{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext=&draw_parameters;vkGetPhysicalDeviceFeatures2(physical,&features);
    const float priority=1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.pNext=&draw_parameters;device_info.pEnabledFeatures=&features.features;
    device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
    VkDevice device=VK_NULL_HANDLE;VK_CHECK(vkCreateDevice(physical,&device_info,nullptr,&device));
    VkQueue queue=VK_NULL_HANDLE;vkGetDeviceQueue(device,family,0,&queue);

    xbg_game_config config{};config.size=sizeof config;config.physical=physical;
    config.device=device;config.queue=queue;config.queue_family=family;
    config.enabled_core=features.features;
    config.enabled_shader_draw_parameters=draw_parameters.shaderDrawParameters;
    config.fence_timeout_ns=5000000000ull;
    config.shader_directory=shader_directory.c_str();
    config.present=unexpected_present;config.log=engine_log;config.fatal=engine_fatal;
    char error[1024]{};
    if(xbg_game_initialize(&config,error,sizeof error)!=0)fail(error);
    check(saw_poll_mode&&saw_no_step&&saw_synchronous_translation,
        "actual engine did not confirm zero-timeout poll mode, no GPU stepping and synchronous translation");

    tex_t texture{};texture.magic=MAGIC('T','E','X',0);
    texture.d.target=NVN_TEXTURE_TARGET_2D;texture.d.format=NVN_FORMAT_RGBA8;
    texture.d.width=texture.d.height=8;texture.d.depth=texture.d.levels=1;
    nvn_tex_layout(&texture.d,&texture.L);
    check(texture.L.size&&texture.L.size<=65536,"unexpected test texture layout");
    void *storage=nullptr;
    check(posix_memalign(&storage,65536,65536)==0,"cannot allocate test texture backing");
    std::memset(storage,0xa5,65536);
    pool_t pool{};pool.magic=MAGIC('P','O','O','L');pool.mem=static_cast<uint8_t*>(storage);pool.size=65536;
    texture.pool=&pool;texture.mem=pool.mem;texture.serial=0x71666c7573680001ull;
    xbg_legacy_pool_add(storage,65536);
    uint8_t pixels[8*8*4];xbg_legacy_pool_add(pixels,sizeof pixels);
    const NVNcopyRegion region{0,0,0,8,8,1};
    uint64_t previous_fence=0;
    size_t checked_pixels=0;
    for(unsigned round=0;round<3;round++){
        sync_t sync{};sync.magic=MAGIC('S','Y','N','C');
        p_clear_tex clear{};clear.tex=&texture;clear.r=region;clear.mask=15;
        clear.c.f[round]=1.0f;clear.c.f[3]=1.0f;
        Stream stream;stream.append(OP_CLEAR_TEX,&clear,sizeof clear);
        sync_t *sync_address=&sync;
        stream.append(OP_FENCE_SYNC,&sync_address,sizeof sync_address);
        stream.append(OP_END,nullptr,0);
        xbg_legacy_pool_add(stream.bytes,stream.size);
        const uint64_t handle=reinterpret_cast<uint64_t>(stream.bytes);
        xbg_submit(1,&handle);
        check(__atomic_load_n(&sync.fenced,__ATOMIC_ACQUIRE)==1,"actual FenceSync did not execute");
        const uint64_t fence=sync.fence;
        check(fence>previous_fence&&fence==xbg_legacy_last_fence(),"FenceSync submission value incorrect");
        previous_fence=fence;

        // Negative control: polling alone must not manufacture completion or
        // flush the still-recording GPU work. There are no other GPU producers.
        unsigned before_polls=0;
        const uint64_t before_deadline=monotonic_ns()+20000000ull;
        do {
            check(xbg_legacy_wait(fence,0)==0,"GPU fence completed before Flush (negative control failed)");
            ++before_polls;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(monotonic_ns()<before_deadline);

        const uint64_t flush_begin=monotonic_ns();
        xbg_legacy_flush();
        const uint64_t flush_ns=monotonic_ns()-flush_begin;
        unsigned after_polls=0;bool complete=false;
        const uint64_t deadline=monotonic_ns()+5000000000ull;
        do {
            ++after_polls;
            if(xbg_legacy_wait(fence,0)==1){complete=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(monotonic_ns()<deadline);
        check(complete,"Flush failed to advance actual GPU work using only zero-timeout polls");
        std::printf("round=%u fence=%llu preflush_incomplete_polls=%u flush_ns=%llu postflush_polls=%u ZERO_TIMEOUT_PROGRESS=PASS\n",
            round,(unsigned long long)fence,before_polls,(unsigned long long)flush_ns,after_polls);
        std::fflush(stdout);

        // This API does a real image-to-buffer GPU readback and may wait for
        // that *new* transfer. It is deliberately after the progress assertion.
        std::memset(pixels,0x5a,sizeof pixels);
        xbg_legacy_read_texels(&texture,0,0,&region,pixels,8*4,sizeof pixels);
        for(unsigned i=0;i<64;i++)for(unsigned channel=0;channel<4;channel++){
            const uint8_t expected=(channel==round||channel==3)?255:0;
            if(pixels[4*i+channel]!=expected){
                std::fprintf(stderr,"round=%u pixel=%u channel=%u actual=%u expected=%u\n",
                    round,i,channel,pixels[4*i+channel],expected);
                fail("actual GPU clear/readback pixel mismatch");
            }
        }
        for(size_t i=0;i<65536;i++)check(pool.mem[i]==0xa5,"GPU clear unexpectedly modified CPU texture backing");
        checked_pixels+=64;
        xbg_legacy_pool_del(stream.bytes);
    }
    xbg_game_stats stats{};xbg_game_get_stats(&stats);
    check(stats.submissions==3&&stats.draws==0&&stats.dispatches==0&&stats.presents==0&&stats.shader_loads==0,
        "unexpected engine operations or asset use");
    xbg_legacy_tex_release(&texture);
    xbg_legacy_pool_del(pixels);xbg_legacy_pool_del(storage);
    check(xbg_game_shutdown()==0,"actual engine shutdown failed");
    std::free(storage);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::printf("queue_flush_gpu_contract PASS real_gpu_clears=3 real_readbacks=3 checked_pixels=%zu "
        "positive_timeout_waits_in_proof=0 finishes=0 presents=0 assets=NONE\n",checked_pixels);
    return 0;
}
