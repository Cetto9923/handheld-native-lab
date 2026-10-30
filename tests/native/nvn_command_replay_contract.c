/* CPU decoder bounds contract. Links the real nvn_command_replay.c; fixture
 * adapters below expose one immutable package and a small guest address pool.
 * No GPU submission/completion or gameplay is simulated by this contract. */
#include "../../src/graphics/nvn_device/nvn_command_replay.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"
#include "../../src/graphics/nvn_device/nvn_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static nvn_command_package fixture;
static unsigned char guest_pool[64];
static unsigned resolver_calls;
static uint64_t last_resolved;
static const uint64_t guest_base=UINT64_C(0x4000000000);
static int fixture_trace;
int nvn_trace_enabled(void) { return fixture_trace; }
int nvn_command_buffer_get_package(uint64_t handle,nvn_command_package *out) {
    if(handle!=fixture.handle)return 0;
    *out=fixture;return 1;
}
const char *nvn_command_buffer_opcode_name(uint32_t op) { (void)op;return "fixture-op"; }
void *nvn_memory_pool_resolve_address(uint64_t address,size_t *available) {
    ++resolver_calls;last_resolved=address;
    if(address<guest_base || address-guest_base>=sizeof guest_pool){*available=0;return NULL;}
    *available=sizeof guest_pool-(size_t)(address-guest_base);
    return guest_pool+(size_t)(address-guest_base);
}
void nvn_sync_family_fence_object(ffrs_sync *object) { (void)object;abort(); }
#define CHECK(expression) do { if(!(expression)){fprintf(stderr,"FAIL line=%d: %s\n",__LINE__,#expression);return 1;} } while(0)

static void record(void *where,uint32_t opcode,const void *payload,size_t bytes) {
    nvn_cb_op_hdr header={opcode,(uint32_t)(sizeof(header)+bytes)};
    memcpy(where,&header,sizeof header);
    if(bytes)memcpy((unsigned char*)where+sizeof header,payload,bytes);
}
static nvn_replay_stats run(const void *bytes,size_t size) {
    fixture=(nvn_command_package){UINT64_C(0x12345678),bytes,size,1,0,NULL};
    resolver_calls=0;last_resolved=0;nvn_command_replay_reset();
    if(!nvn_command_replay_run(fixture.handle))abort();
    nvn_replay_stats stats;nvn_command_replay_get_stats(&stats);return stats;
}
static int adjacent(int indexed) {
    /* The old reader saw 0x000000f800000008 (the SET_RT header) as ind. */
    unsigned char bytes[40+248+8]={0};
    nvn_cb_p_draw draw={0};nvn_cb_p_draw_idx index={0};nvn_cb_p_set_rt rt={0};
    size_t payload=indexed?offsetof(nvn_cb_p_draw_idx,ind):offsetof(nvn_cb_p_draw,ind);
    const size_t first=8+payload;
    record(bytes,indexed?NVN_CB_OP_DRAW_IDX:NVN_CB_OP_DRAW,indexed?(const void*)&index:(const void*)&draw,payload);
    record(bytes+first,NVN_CB_OP_SET_RT,&rt,240);
    record(bytes+first+248,NVN_CB_OP_END,NULL,0);
    nvn_replay_stats stats=run(bytes,first+248+8);
    CHECK(stats.records==3&&stats.draws==1&&stats.refused_no_pool==0);
    CHECK(resolver_calls==0);
    return 0;
}
static int guarded(int indexed) {
    const long page=sysconf(_SC_PAGESIZE);CHECK(page>0);
    void *mapping=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(mapping!=MAP_FAILED);
    CHECK(mprotect((unsigned char*)mapping+page,(size_t)page,PROT_NONE)==0);
    nvn_cb_p_draw draw={0};nvn_cb_p_draw_idx index={0};
    const size_t payload=indexed?offsetof(nvn_cb_p_draw_idx,ind):offsetof(nvn_cb_p_draw,ind);
    const size_t size=8+payload;
    unsigned char *bytes=(unsigned char*)mapping+page-size;
    record(bytes,indexed?NVN_CB_OP_DRAW_IDX:NVN_CB_OP_DRAW,indexed?(const void*)&index:(const void*)&draw,payload);
    nvn_replay_stats stats=run(bytes,size);
    CHECK(stats.records==1&&stats.draws==1&&stats.refused_no_pool==0&&resolver_calls==0);
    CHECK(munmap(mapping,(size_t)page*2)==0);
    return 0;
}
static int indirect(int indexed,uint64_t address,int valid) {
    unsigned char bytes[64+8]={0};
    nvn_cb_p_draw draw={0};nvn_cb_p_draw_idx index={0};
    draw.ind=address;index.ind=address;
    const size_t payload=indexed?sizeof index:sizeof draw;
    record(bytes,indexed?NVN_CB_OP_DRAW_IDX:NVN_CB_OP_DRAW,indexed?(const void*)&index:(const void*)&draw,payload);
    record(bytes+8+payload,NVN_CB_OP_END,NULL,0);
    const nvn_replay_stats stats=run(bytes,8+payload+8);
    CHECK(resolver_calls==1&&last_resolved==address);
    CHECK(stats.refused_no_pool==(uint64_t)!valid);
    CHECK(stats.resolved_addresses==(uint64_t)valid);
    return 0;
}
static int indexed_buffer(void) {
    unsigned char bytes[48]={0};nvn_cb_p_draw_idx index={0};index.ibuf=guest_base+sizeof guest_pool;
    record(bytes,NVN_CB_OP_DRAW_IDX,&index,offsetof(nvn_cb_p_draw_idx,ind));
    record(bytes+40,NVN_CB_OP_END,NULL,0);
    const nvn_replay_stats stats=run(bytes,sizeof bytes);
    CHECK(stats.refused_no_pool==1&&resolver_calls==1&&last_resolved==index.ibuf);
    return 0;
}
static int repeated_pool_diagnostics(unsigned count, int verbose) {
    const size_t stride=8+sizeof(nvn_cb_p_bind_tex1);
    const size_t bytes=count*stride+8;
    unsigned char *commands=calloc(1,bytes);CHECK(commands);
    for(unsigned i=0;i<count;i++) {
        nvn_cb_p_bind_tex1 bind={0};bind.h=(i&1)?UINT64_C(1)<<20:1;
        record(commands+i*stride,NVN_CB_OP_BIND_TEX1,&bind,sizeof bind);
    }
    record(commands+count*stride,NVN_CB_OP_END,NULL,0);
    fixture_trace=verbose;
    nvn_replay_stats stats=run(commands,bytes);
    fixture_trace=0;
    CHECK(stats.records==count+1 && stats.refused_foreign_pool==count);
    CHECK(stats.missing_texture_pool==count/2 && stats.missing_sampler_pool==count/2);
    CHECK(stats.refused_bad_record==0 && stats.resolved_addresses==0);
    CHECK(stats.timed_packages==1 && stats.elapsed_ns>0 && stats.thread_cpu_ns>0);
    if(verbose)CHECK(stats.pool_messages_printed==count);
    else CHECK(stats.pool_messages_printed>0 && stats.pool_messages_printed<=40);
    free(commands);return 0;
}

int main(int argc,char **argv) {
    const char *which=argc>1?argv[1]:"all";
    CHECK(argc<=2);
    if(!strcmp(which,"all")){CHECK(repeated_pool_diagnostics(10000,0)==0);CHECK(repeated_pool_diagnostics(16,1)==0);}
    if(!strcmp(which,"short-draw")||!strcmp(which,"all"))CHECK(adjacent(0)==0);
    if(!strcmp(which,"short-index")||!strcmp(which,"all"))CHECK(adjacent(1)==0);
    if(!strcmp(which,"guard-draw")||!strcmp(which,"all"))CHECK(guarded(0)==0);
    if(!strcmp(which,"guard-index")||!strcmp(which,"all"))CHECK(guarded(1)==0);
    if(!strcmp(which,"indirect")||!strcmp(which,"all")){
        for(int indexed=0;indexed<2;indexed++){
            CHECK(indirect(indexed,guest_base,1)==0);
            CHECK(indirect(indexed,guest_base+sizeof guest_pool-20,1)==0);
            CHECK(indirect(indexed,guest_base+sizeof guest_pool,0)==0);
            CHECK(indirect(indexed,guest_base-8,0)==0);
            CHECK(indirect(indexed,UINT64_C(0xf800000008),0)==0);
        }
        CHECK(indexed_buffer()==0);
    }
    CHECK(!strcmp(which,"all")||!strcmp(which,"short-draw")||!strcmp(which,"short-index")||
        !strcmp(which,"guard-draw")||!strcmp(which,"guard-index")||!strcmp(which,"indirect"));
    printf("nvn_command_replay_contract PASS case=%s short_draw=bounded indirect_address_validation=preserved GPU=NOT_TESTED\n",which);
    return 0;
}
