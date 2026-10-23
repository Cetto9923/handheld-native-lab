/* SPDX-License-Identifier: MIT — ordinary owned contracts, no game inputs. */
#include <vulkan_core.h>
#if defined(FFRS_LAS_DRIVER_MOCK)
/* Reuse the existing host driver double unchanged, extending its barrier
 * observer for the production adapter's core VkMemoryBarrier calls. NEVER GPU
 * execution evidence. This branch is not linked into the Android driver. */
#define vkCmdPipelineBarrier las_legacy_mock_barrier
#include "vulkan_mock_driver.c"
#undef vkCmdPipelineBarrier
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer command,VkPipelineStageFlags src,
    VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t memory_count,const VkMemoryBarrier *memory,
    uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images) {
    if(memory_count) {
        assert(memory_count==1&&memory&&!buffer_count&&!image_count&&!flags);
        assert(memory->sType==VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        assert((src&VK_PIPELINE_STAGE_TRANSFER_BIT)&&(dst&(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT)));
        return;
    }
    las_legacy_mock_barrier(command,src,dst,flags,0,NULL,buffer_count,buffers,image_count,images);
}
#else
#include "../../src/graphics/nvn_las/typed_transfer.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
typedef struct SeenCopy {VkBuffer source,destination;VkBufferCopy region;} SeenCopy;
static SeenCopy seen[32];static unsigned ncopy,nbarrier;
static int device_token,command_token,source_token,destination_token,memory_tokens[2],fence_tokens[2];
static VkDevice device(void){return (VkDevice)&device_token;}
static VkCommandBuffer command(void){return (VkCommandBuffer)&command_token;}
static VkFence fence(void){return (VkFence)&fence_tokens[0];}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer cmd,VkPipelineStageFlags src,
    VkPipelineStageFlags dst,VkDependencyFlags flags,uint32_t count,const VkMemoryBarrier *memory,
    uint32_t buffer_count,const VkBufferMemoryBarrier *buffers,uint32_t image_count,const VkImageMemoryBarrier *images) {
    (void)buffers;(void)images;assert(cmd==command()&&src&&dst&&!flags&&count==1&&memory);
    assert(!buffer_count&&!image_count&&memory->sType==VK_STRUCTURE_TYPE_MEMORY_BARRIER);++nbarrier;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer cmd,VkBuffer src,VkBuffer dst,
                                         uint32_t count,const VkBufferCopy *region) {
    assert(cmd==command()&&count==1&&region&&ncopy<32);
    seen[ncopy++]=(SeenCopy){src,dst,*region}; /* observe only; no memcpy or GPU claim */
}
static FfrsVkBufferResource resource(unsigned which,unsigned char *mapped) {
    return (FfrsVkBufferResource){.device=device(),.buffer=(VkBuffer)(which?&destination_token:&source_token),
        .memory=(VkDeviceMemory)&memory_tokens[which],.mapped=mapped,.bytes=256,.allocation_bytes=512,
        .memory_flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};
}
static void (*target(const char *name))(void) {
    size_t count;const FfrsLasTypedTarget *t=ffrs_las_typed_targets(&count);assert(count==8);
    for(size_t i=0;i<count;++i) {
        assert(t[i].signature.version==1&&t[i].signature.flags==NB_SIG_REGISTER_ONLY);
        assert(!t[i].signature.stack_argument_bytes&&!t[i].signature.fp_arguments);
        if(!strcmp(t[i].name,name))return t[i].host_target;
    }
    assert(0);return NULL;
}
typedef struct FixedRecord {uint32_t op,size;uint64_t x[7];float f[4];} FixedRecord;
_Static_assert(sizeof(FixedRecord)==sizeof(CmdRec),"test record ABI");
static void list(FixedRecord records[3]) {
    memset(records,0,3*sizeof(*records));
    records[0].op=CMD_BARRIER;records[1].op=CMD_COPY_BUFFER_TO_BUFFER;records[2].op=CMD_END;
    for(unsigned i=0;i<3;++i)records[i].size=sizeof(CmdRec);
    records[1].x[0]=0x1004;records[1].x[1]=0x2008;records[1].x[2]=64;
}
static void bind_pair(FfrsLasBackend *b,FfrsVkBufferResource *src,FfrsVkBufferResource *dst) {
    assert(ffrs_las_backend_init(b,device())==FFRS_LAS_OK);
    assert(ffrs_las_backend_bind(b,0x1000,256,src,0,VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT)==FFRS_LAS_OK);
    assert(ffrs_las_backend_bind(b,0x2000,256,dst,0,VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT)==FFRS_LAS_OK);
}
typedef struct ThreadCheck {FfrsLasBackend *backend;FfrsLasTyped *typed;uint64_t identity;} ThreadCheck;
static void *other_thread(void *opaque) {
    ThreadCheck *t=opaque;t->identity=ffrs_las_thread_id();
    assert(ffrs_las_backend_check(t->backend)==FFRS_LAS_WRONG_THREAD);
    assert(ffrs_las_backend_unbind(t->backend,0x1000)==FFRS_LAS_WRONG_THREAD);
    assert(ffrs_las_typed_enter(t->typed)==FFRS_LAS_WRONG_THREAD);
    PFNNVNCOMMANDBUFFERINITIALIZEPROC initialize=(PFNNVNCOMMANDBUFFERINITIALIZEPROC)target("nvnCommandBufferInitialize");
    assert(!initialize(NULL,NULL)&&ffrs_las_typed_status(NULL)==FFRS_LAS_NO_ACTIVE);
    FfrsLasBackend own={0};assert(ffrs_las_backend_init(&own,device())==FFRS_LAS_OK);
    assert(ffrs_las_backend_close(&own)==FFRS_LAS_OK);return NULL;
}
int main(void) {
    unsigned char src_bytes[512],dst_bytes[512];memset(src_bytes,0x81,sizeof(src_bytes));memset(dst_bytes,0x24,sizeof(dst_bytes));
    FfrsVkBufferResource src=resource(0,src_bytes),dst=resource(1,dst_bytes);
    FfrsLasBackend b={0};bind_pair(&b,&src,&dst);
    assert(ffrs_las_backend_init(&b,device())==FFRS_LAS_BUSY);
    assert(ffrs_las_backend_bind(&b,UINT64_MAX-4,8,&src,0,VK_BUFFER_USAGE_TRANSFER_SRC_BIT)==FFRS_LAS_INVALID);
    assert(ffrs_las_backend_bind(&b,0x1040,16,&dst,0,VK_BUFFER_USAGE_TRANSFER_SRC_BIT)==FFRS_LAS_INVALID);
    assert(ffrs_las_backend_close(&b)==FFRS_LAS_BUSY);
    NVNdevice device_key;NVNmemoryPool pool_key;NVNcommandBuffer object_key;
    memset(&device_key,0xa7,sizeof(device_key));memset(&pool_key,0xb8,sizeof(pool_key));memset(&object_key,0xc9,sizeof(object_key));
    struct Guard {uint64_t before;unsigned char records[4*sizeof(CmdRec)];uint64_t after;} storage;
    storage.before=UINT64_C(0x1234567887654321);storage.after=UINT64_C(0x9876543223456789);
    FfrsLasTyped c={0};assert(ffrs_las_typed_init(&c,&b,&device_key)==FFRS_LAS_OK);
    assert(ffrs_las_typed_attach_pool(&c,&pool_key,storage.records,sizeof(storage.records))==FFRS_LAS_OK);
    PFNNVNCOMMANDBUFFERINITIALIZEPROC initialize=(PFNNVNCOMMANDBUFFERINITIALIZEPROC)target("nvnCommandBufferInitialize");
    PFNNVNCOMMANDBUFFERADDCOMMANDMEMORYPROC memory=(PFNNVNCOMMANDBUFFERADDCOMMANDMEMORYPROC)target("nvnCommandBufferAddCommandMemory");
    PFNNVNCOMMANDBUFFERBEGINRECORDINGPROC begin=(PFNNVNCOMMANDBUFFERBEGINRECORDINGPROC)target("nvnCommandBufferBeginRecording");
    PFNNVNCOMMANDBUFFERENDRECORDINGPROC end=(PFNNVNCOMMANDBUFFERENDRECORDINGPROC)target("nvnCommandBufferEndRecording");
    PFNNVNCOMMANDBUFFERCOPYBUFFERTOBUFFERPROC copy=(PFNNVNCOMMANDBUFFERCOPYBUFFERTOBUFFERPROC)target("nvnCommandBufferCopyBufferToBuffer");
    PFNNVNCOMMANDBUFFERFINALIZEPROC finalize=(PFNNVNCOMMANDBUFFERFINALIZEPROC)target("nvnCommandBufferFinalize");
    assert(!initialize(&object_key,&device_key)&&ffrs_las_typed_status(NULL)==FFRS_LAS_NO_ACTIVE);
    assert(ffrs_las_typed_enter(&c)==FFRS_LAS_OK&&ffrs_las_typed_enter(&c)==FFRS_LAS_BUSY);
    assert(initialize(&object_key,&device_key));memory(&object_key,&pool_key,0,sizeof(storage.records));
    copy(&object_key,0x1000,0x2000,64,0);assert(ffrs_las_typed_status(&c)==FFRS_LAS_INVALID);
    assert(ffrs_las_typed_clear_error(&c)==FFRS_LAS_OK);
    begin(&object_key);copy(&object_key,0x1004,0x2008,64,0);uint64_t handle=end(&object_key);assert(handle);
    assert(ffrs_las_typed_status(&c)==FFRS_LAS_OK&&ffrs_las_typed_leave(&c)==FFRS_LAS_OK);
    assert(ffrs_las_typed_encode(&c,command(),handle)==FFRS_LAS_OK);
    assert(ncopy==1&&nbarrier==3&&b.copy_count==1&&b.copied_bytes==64);
    assert(seen[0].source==src.buffer&&seen[0].destination==dst.buffer&&seen[0].region.srcOffset==4&&seen[0].region.dstOffset==8&&seen[0].region.size==64);
    for(unsigned i=0;i<512;++i)assert(src_bytes[i]==0x81&&dst_bytes[i]==0x24); /* no CPU fake copy */
    assert(ffrs_las_backend_unbind(&b,0x1000)==FFRS_LAS_BUSY&&ffrs_las_backend_cancel(&b,false)==FFRS_LAS_BUSY);
    assert(ffrs_las_backend_submitted(&b,fence(),VK_SUCCESS)==FFRS_LAS_OK);
    assert(ffrs_las_backend_waited(&b,(VkFence)&fence_tokens[1],VK_SUCCESS)==FFRS_LAS_INVALID);
    assert(b.phase==FFRS_LAS_PENDING);
    assert(ffrs_las_backend_waited(&b,fence(),VK_TIMEOUT)==FFRS_LAS_QUARANTINED);
    assert(ffrs_las_backend_unbind(&b,0x1000)==FFRS_LAS_QUARANTINED&&src.buffer==(VkBuffer)&source_token);
    assert(ffrs_las_backend_waited(&b,fence(),VK_SUCCESS)==FFRS_LAS_OK);
    assert(ffrs_las_backend_retire(&b)==FFRS_LAS_OK);
    ThreadCheck checks[2]={{&b,&c,0},{&b,&c,0}};pthread_t thread;
    for(unsigned i=0;i<2;++i){assert(!pthread_create(&thread,NULL,other_thread,&checks[i]));assert(!pthread_join(thread,NULL));}
    assert(checks[0].identity!=checks[1].identity&&checks[0].identity!=b.owner);
    assert(ffrs_las_typed_enter(&c)==FFRS_LAS_OK);
    begin(&object_key);uint64_t replacement=end(&object_key);assert(replacement&&replacement!=handle);
    assert(ffrs_las_typed_encode(&c,command(),handle)==FFRS_LAS_INVALID&&ncopy==1);
    begin(&object_key);copy(&object_key,0x1000,0x2000,64,1);assert(!end(&object_key));
    assert(ffrs_las_typed_status(&c)==FFRS_LAS_UNSUPPORTED);
    assert(ffrs_las_typed_clear_error(&c)==FFRS_LAS_OK);
    memory(&object_key,&pool_key,0,2*sizeof(CmdRec));begin(&object_key);copy(&object_key,0x1000,0x2000,64,0);
    assert(ffrs_las_typed_status(&c)==FFRS_LAS_LIMIT&&!end(&object_key));
    assert(ffrs_las_typed_clear_error(&c)==FFRS_LAS_OK);finalize(&object_key);
    assert(ffrs_las_typed_leave(&c)==FFRS_LAS_OK);
    assert(ffrs_las_typed_close(&c)==FFRS_LAS_OK);
    for(size_t i=0;i<sizeof(device_key);++i)assert(((unsigned char*)&device_key)[i]==0xa7);
    for(size_t i=0;i<sizeof(pool_key);++i)assert(((unsigned char*)&pool_key)[i]==0xb8);
    for(size_t i=0;i<sizeof(object_key);++i)assert(((unsigned char*)&object_key)[i]==0xc9);
    assert(storage.before==UINT64_C(0x1234567887654321)&&storage.after==UINT64_C(0x9876543223456789));
    unsigned before_copy=ncopy,before_barrier=nbarrier;FixedRecord records[3];
    for(unsigned test=0;test<11;++test) {
        list(records);
        switch(test) {
        case 0:records[1].op=CMD_DRAW_ARRAYS;break;
        case 1:records[1].op=CMD_JUMP;break;
        case 2:records[1].op=CMD_CALL;break;
        case 3:records[2].op=CMD_DRAW_ARRAYS;break;
        case 4:records[1].size=8;break;
        case 5:records[1].x[3]=1;break;
        case 6:records[1].x[0]=0x1001;break;
        case 7:records[1].x[0]=UINT64_MAX-4;break;
        case 8:records[1].x[2]=0;break;
        case 9:records[1].x[1]=0x1008;break;
        case 10:records[0].op=CMD_END;break;
        }
        assert(ffrs_las_backend_encode(&b,command(),records,sizeof(records))!=FFRS_LAS_OK);
        assert(ncopy==before_copy&&nbarrier==before_barrier&&b.phase==FFRS_LAS_IDLE);
    }
    list(records);assert(ffrs_las_backend_encode(&b,command(),records,sizeof(records)-1)==FFRS_LAS_INVALID);
    assert(ffrs_las_backend_encode(&b,command(),records,(FFRS_LAS_MAX_RECORDS+1)*sizeof(CmdRec))==FFRS_LAS_LIMIT);
    assert(ffrs_las_backend_encode(&b,command(),records,sizeof(records))==FFRS_LAS_OK);
    assert(ffrs_las_backend_cancel(&b,true)==FFRS_LAS_OK); /* owned mock command discarded */
    assert(ffrs_las_backend_unbind(&b,0x1000)==FFRS_LAS_OK&&ffrs_las_backend_unbind(&b,0x2000)==FFRS_LAS_OK);
    assert(ffrs_las_backend_close(&b)==FFRS_LAS_OK);
    for(unsigned which=0;which<2;++which) {
        FfrsLasBackend quarantined={0};bind_pair(&quarantined,&src,&dst);
        assert(ffrs_las_backend_encode(&quarantined,command(),records,sizeof(records))==FFRS_LAS_OK);
        if(!which)assert(ffrs_las_backend_submitted(&quarantined,fence(),VK_ERROR_DEVICE_LOST)==FFRS_LAS_QUARANTINED);
        else {assert(ffrs_las_backend_submitted(&quarantined,fence(),VK_SUCCESS)==FFRS_LAS_OK);
              assert(ffrs_las_backend_waited(&quarantined,fence(),VK_ERROR_DEVICE_LOST)==FFRS_LAS_QUARANTINED);}
        assert(ffrs_las_backend_waited(&quarantined,fence(),VK_SUCCESS)==FFRS_LAS_QUARANTINED);
        assert(ffrs_las_backend_unbind(&quarantined,0x1000)==FFRS_LAS_QUARANTINED);
        assert(ffrs_las_backend_cancel(&quarantined,true)==FFRS_LAS_QUARANTINED);
        assert(ffrs_las_backend_retire(&quarantined)==FFRS_LAS_QUARANTINED);
        assert(ffrs_las_backend_close(&quarantined)==FFRS_LAS_QUARANTINED);
        assert(quarantined.buffers[0].resource==&src&&quarantined.buffers[1].resource==&dst);
        /* Ordinary injected result contract: no real submission happened, fake
         * handles are stack sentinels. Production has no teardown shortcut. */
    }
    puts("{\"status\":\"PASS\",\"scope\":\"las_transfer_host_contract_only\",\"gpu_execution\":\"NOT_RUN\","
         "\"typed_to_record_to_vk_copy\":true,\"opaque_bytes_unchanged\":true,\"whole_list_preflight\":true,"
         "\"owner_threads\":true,\"stale_handles_rejected\":true,\"unsupported_and_capacity\":true,"
         "\"timeout_retry\":true,\"submit_and_device_lost_quarantine\":true,\"no_cpu_copy_fallback\":true}");
    return 0;
}
#endif
