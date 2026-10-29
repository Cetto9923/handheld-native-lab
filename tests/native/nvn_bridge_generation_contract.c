/* CPU contract: actual pool/texture families and bridge, no Vulkan or guest code.
 * Device/engine hooks below are explicit seams, never a device acceptance. */
#include "../../src/graphics/nvn_device/nvn_bridge.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"
#include "../../src/graphics/nvn_device/nvn_texture_pool.h"
#include "../../src/graphics/nvn_device/nvn_sampler_pool.h"
#include "../../src/graphics/nvn_device/nvn_texture.h"
#include "../../src/graphics/nvn_device/nvn_texture_builder.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void nvn_memory_pool_builder_set_defaults(void*);
extern void nvn_memory_pool_builder_set_device(void*,void*);
extern void nvn_memory_pool_builder_set_storage(void*,void*,uint64_t);
extern void nvn_memory_pool_builder_set_flags(void*,int);
extern int nvn_memory_pool_initialize(void*,const void*);
extern void nvn_memory_pool_finalize(void*);
extern int nvn_texture_pool_initialize(void*,const void*,ptrdiff_t,int);
extern void nvn_texture_pool_finalize(void*);
extern int nvn_sampler_pool_initialize(void*,const void*,ptrdiff_t,int);
extern void nvn_sampler_pool_finalize(void*);
static unsigned errors, adds, dels, retires;
static unsigned char device;
#define EXPECT(x) do { if (!(x)) { ++errors; fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); } } while(0)
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FATAL line=%d %s\n",__LINE__,#x); exit(2); } } while(0)
int nvn_device_is_ready(const void *d) { return d==&device; }
uint32_t nvn_device_generate_debug_domain_id(void *d,const char *s) {(void)d;(void)s;return 1;}
int nvn_engine_ready(void) { return 0; }
void nvn_engine_pool_add(void *p,size_t n) { REQUIRE(p&&n); ++adds; }
void nvn_engine_pool_del(void *p) { REQUIRE(p); ++dels; }
void nvn_command_buffer_retire_source_memory(void *p,size_t n) { REQUIRE(p&&n); ++retires; }
int nvn_engine_register_texture(const void *p,int i,const void *t,const void *v) {(void)p;(void)i;(void)t;(void)v;return 0;}
int nvn_engine_register_sampler(const void *p,int i,const void *s) {(void)p;(void)i;(void)s;return 0;}
int nvn_program_info_of(const void *p,nvn_program_info *o) {(void)p;(void)o;return 0;}
int nvn_sampler_info_of(const void *p,ffrs_samp_par *o,uint64_t *s) {(void)p;(void)o;(void)s;return 0;}
int nvn_texture_view_info_of(const void *p,ffrs_view *o,uint64_t *s) {(void)p;(void)o;(void)s;return 0;}
/* Family entry lookup uses production tables, including actual builder/texture. */
static void *builder_fn(const char *s) {
    for(size_t i=0;i<nvn_texture_builder_family_count();++i){const nvn_texture_builder_family_entry *e=nvn_texture_builder_family_entry_at(i);if(!strcmp(s,e->name))return e->fn;}
    return NULL;
}
static void *texture_fn(const char *s) {
    for(size_t i=0;i<nvn_texture_family_count();++i){const nvn_texture_family_entry *e=nvn_texture_family_entry_at(i);if(!strcmp(s,e->name))return e->fn;}
    return NULL;
}
static void make_texture(void *t,void *b,void *p) {
    void (*defaults)(void*)=builder_fn("nvnTextureBuilderSetDefaults");
    void (*set_device)(void*,void*)=builder_fn("nvnTextureBuilderSetDevice");
    void (*format)(void*,int)=builder_fn("nvnTextureBuilderSetFormat");
    void (*size)(void*,int,int)=builder_fn("nvnTextureBuilderSetSize2D");
    void (*storage)(void*,const void*,ptrdiff_t)=builder_fn("nvnTextureBuilderSetStorage");
    int (*init)(void*,const void*)=texture_fn("nvnTextureInitialize");
    REQUIRE(defaults&&set_device&&format&&size&&storage&&init);
    defaults(b);set_device(b,&device);format(b,0x37);size(b,4,4);storage(b,p,128);REQUIRE(init(t,b));
}
typedef struct {const void *guest;const ffrs_pool *memory;const ffrs_dpool *texture,*sampler;} race_result;
static pthread_mutex_t barrier_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_cond=PTHREAD_COND_INITIALIZER;
static unsigned arrivals;static int release_readers;
static void *reader(void *v) {
    race_result *r=v;
    pthread_mutex_lock(&barrier_lock);++arrivals;pthread_cond_broadcast(&barrier_cond);
    while(!release_readers)pthread_cond_wait(&barrier_cond,&barrier_lock);
    pthread_mutex_unlock(&barrier_lock);
    r->memory=nvn_bridge_pool(r->guest);
    r->texture=nvn_bridge_texture_pool((const unsigned char*)r->guest+1);
    r->sampler=nvn_bridge_sampler_pool((const unsigned char*)r->guest+2);
    return NULL;
}
int main(void) {
    _Alignas(8) unsigned char pool[64],mb[64],tp[64],sp[64],tb[128],tex[64],a[8192],b[16384];
    memset(pool,0xa5,sizeof(pool));memset(a,0x11,sizeof(a));memset(b,0x22,sizeof(b));
    nvn_memory_pool_builder_set_defaults(mb);nvn_memory_pool_builder_set_device(mb,&device);
    nvn_memory_pool_builder_set_storage(mb,a,sizeof(a));nvn_memory_pool_builder_set_flags(mb,0x102);
    REQUIRE(nvn_memory_pool_initialize(pool,mb));
    const ffrs_pool *old=nvn_bridge_pool(pool);REQUIRE(old);
    EXPECT(old->mem==a&&old->size==sizeof(a)&&old->flags==0x102);EXPECT(nvn_bridge_pool(pool)==old);
    REQUIRE(nvn_texture_pool_initialize(tp,pool,256,16));REQUIRE(nvn_sampler_pool_initialize(sp,pool,1024,8));
    const ffrs_dpool *ot=nvn_bridge_texture_pool(tp),*os=nvn_bridge_sampler_pool(sp);REQUIRE(ot&&os);
    EXPECT(ot->pool==old&&ot->off==256&&ot->n==16);EXPECT(os->pool==old&&os->off==1024&&os->n==8);
    make_texture(tex,tb,pool);const ffrs_tex *oldtex=nvn_bridge_texture(tex);REQUIRE(oldtex);
    EXPECT(oldtex->pool==old&&oldtex->mem==a+128);
    ffrs_pool old_value=*old;ffrs_dpool ot_value=*ot,os_value=*os;ffrs_tex oldtex_value=*oldtex;
    nvn_texture_pool_finalize(tp);nvn_sampler_pool_finalize(sp);nvn_memory_pool_finalize(pool);
    EXPECT(!nvn_bridge_pool(pool));EXPECT(!nvn_bridge_texture_pool(tp));EXPECT(!nvn_bridge_sampler_pool(sp));
    nvn_memory_pool_builder_set_storage(mb,b,sizeof(b));nvn_memory_pool_builder_set_flags(mb,0x204);
    REQUIRE(nvn_memory_pool_initialize(pool,mb));const ffrs_pool *now=nvn_bridge_pool(pool);REQUIRE(now);
    EXPECT(now!=old);EXPECT(now->mem==b&&now->size==sizeof(b)&&now->flags==0x204);EXPECT(nvn_bridge_pool(pool)==now);
    REQUIRE(nvn_texture_pool_initialize(tp,pool,512,32));REQUIRE(nvn_sampler_pool_initialize(sp,pool,2048,12));
    const ffrs_dpool *nt=nvn_bridge_texture_pool(tp),*ns=nvn_bridge_sampler_pool(sp);REQUIRE(nt&&ns);
    EXPECT(nt!=ot&&nt->pool==now&&nt->off==512&&nt->n==32);EXPECT(ns!=os&&ns->pool==now&&ns->off==2048&&ns->n==12);
    make_texture(tex,tb,pool);const ffrs_tex *newtex=nvn_bridge_texture(tex);REQUIRE(newtex);
    EXPECT(newtex!=oldtex&&newtex->pool==now&&newtex->mem==b+128);EXPECT(nvn_bridge_texture(tex)==newtex);
    /* Reinitialize all objects with identical parameters: identity still changes. */
    nvn_texture_pool_finalize(tp);nvn_sampler_pool_finalize(sp);nvn_memory_pool_finalize(pool);
    REQUIRE(nvn_memory_pool_initialize(pool,mb));const ffrs_pool *same=nvn_bridge_pool(pool);REQUIRE(same);
    REQUIRE(nvn_texture_pool_initialize(tp,pool,512,32));REQUIRE(nvn_sampler_pool_initialize(sp,pool,2048,12));
    const ffrs_dpool *st=nvn_bridge_texture_pool(tp),*ss=nvn_bridge_sampler_pool(sp);REQUIRE(st&&ss);
    EXPECT(same!=now&&same->mem==b&&same->size==sizeof(b)&&same->flags==0x204);
    EXPECT(st!=nt&&ss!=ns&&st->pool==same&&ss->pool==same);
    EXPECT(nvn_bridge_texture_pool(tp)==st&&nvn_bridge_sampler_pool(sp)==ss);
    EXPECT(!memcmp(old,&old_value,sizeof(*old)));EXPECT(!memcmp(ot,&ot_value,sizeof(*ot)));EXPECT(!memcmp(os,&os_value,sizeof(*os)));
    EXPECT(!memcmp(oldtex,&oldtex_value,sizeof(*oldtex)));EXPECT(oldtex->mem[0]==0x11);
    /* Descriptor identity must also follow a changed backing snapshot even when
     * its own family generation has not changed. Missing backing fails closed. */
    nvn_memory_pool_finalize(pool);
    EXPECT(!nvn_bridge_texture_pool(tp)&&!nvn_bridge_sampler_pool(sp));
    REQUIRE(nvn_memory_pool_initialize(pool,mb));
    const ffrs_pool *backing_again=nvn_bridge_pool(pool);REQUIRE(backing_again);
    const ffrs_dpool *bt=nvn_bridge_texture_pool(tp),*bs=nvn_bridge_sampler_pool(sp);REQUIRE(bt&&bs);
    EXPECT(backing_again!=same&&bt!=st&&bs!=ss&&bt->pool==backing_again&&bs->pool==backing_again);
    EXPECT(st->pool==same&&ss->pool==same); /* immutable earlier dependency */
    /* Grow the bridge metadata table and recycle the family's slot repeatedly.
     * Old command pointers refer to allocations, not table interiors. */
    unsigned char identities[80];
    for(unsigned i=0;i<sizeof(identities);++i){
        REQUIRE(nvn_memory_pool_initialize(identities+i,mb));
        const ffrs_pool *g=nvn_bridge_pool(identities+i);REQUIRE(g);EXPECT(g->mem==b&&g->size==sizeof(b));
        EXPECT(nvn_bridge_pool(identities+i)==g);nvn_memory_pool_finalize(identities+i);
        EXPECT(!nvn_bridge_pool(identities+i));EXPECT(!memcmp(old,&old_value,sizeof(*old)));
    }
    /* Concurrent cache publication for one new generation must return one copy. */
    unsigned char race_pool[8];
    REQUIRE(nvn_memory_pool_initialize(race_pool,mb));
    REQUIRE(nvn_texture_pool_initialize(race_pool+1,race_pool,768,3));
    REQUIRE(nvn_sampler_pool_initialize(race_pool+2,race_pool,2304,5));
    enum {N=16};pthread_t threads[N];race_result results[N];
    for(unsigned i=0;i<N;++i){memset(&results[i],0,sizeof(results[i]));results[i].guest=race_pool;REQUIRE(!pthread_create(&threads[i],NULL,reader,&results[i]));}
    pthread_mutex_lock(&barrier_lock);while(arrivals<N)pthread_cond_wait(&barrier_cond,&barrier_lock);release_readers=1;pthread_cond_broadcast(&barrier_cond);pthread_mutex_unlock(&barrier_lock);
    for(unsigned i=0;i<N;++i){REQUIRE(!pthread_join(threads[i],NULL));EXPECT(results[i].memory&&results[i].texture&&results[i].sampler);EXPECT(results[i].memory==results[0].memory&&results[i].texture==results[0].texture&&results[i].sampler==results[0].sampler);}
    nvn_texture_pool_finalize(race_pool+1);nvn_sampler_pool_finalize(race_pool+2);nvn_memory_pool_finalize(race_pool);
    nvn_texture_pool_finalize(tp);nvn_sampler_pool_finalize(sp);nvn_memory_pool_finalize(pool);
    EXPECT(!nvn_bridge_pool(pool)&&!nvn_bridge_texture_pool(tp)&&!nvn_bridge_sampler_pool(sp));
    EXPECT(adds==85&&dels==85&&retires==85);EXPECT(pool[0]==0xa5);
    printf("nvn_bridge_generation errors=%u adds=%u dels=%u source_retires=%u CPU_ONLY=1\n",errors,adds,dels,retires);
    return errors?1:0;
}
