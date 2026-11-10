/* CPU ownership contract. Families and renderer retirement are explicit seams;
 * the production bridge is included to count its texture allocations only. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "../../src/graphics/nvn_device/nvn_bridge.h"
static unsigned texture_allocations, retirements;
static void *counted_calloc(size_t n,size_t size) {
    if(n==1 && size==sizeof(ffrs_tex)) ++texture_allocations;
    return calloc(n,size);
}
#define calloc counted_calloc
#include "../../src/graphics/nvn_device/nvn_bridge.c"
#undef calloc
static unsigned char backing[8192], guest, pool_guest;
static nvn_texture_info current;
static ffrs_tex retired;
static int available=1;
int nvn_texture_info_of(const void *p,nvn_texture_info *o) {
    if(p!=&guest || !available)return 0;
    *o=current;return 1;
}
int nvn_memory_pool_info_of(const void *p,nvn_memory_pool_info *o) {
    if(p!=&pool_guest)return 0;
    memset(o,0,sizeof(*o));o->mem=backing;o->size=sizeof(backing);o->generation=1;return 1;
}
int nvn_program_info_of(const void *p,nvn_program_info *o){(void)p;(void)o;return 0;}
int nvn_texture_pool_info_of(const void *p,nvn_pool_info *o){(void)p;(void)o;return 0;}
int nvn_sampler_pool_info_of(const void *p,nvn_pool_info *o){(void)p;(void)o;return 0;}
int nvn_sampler_info_of(const void *p,ffrs_samp_par *o,uint64_t *n){(void)p;(void)o;(void)n;return 0;}
int nvn_texture_view_info_of(const void *p,ffrs_view *o,uint64_t *n){(void)p;(void)o;(void)n;return 0;}
void nvn_tex_layout(const nvn_texdesc *d,nvn_texlayout *o){memset(o,0,sizeof(*o));o->size=d->width*4;}
void xbg_texture_lifetime_end(const ffrs_tex *t){retired=*t;++retirements;}
static void *reader(void *unused) {
    (void)unused;
    for(unsigned i=0;i<10000;i++) {
        ffrs_tex value;assert(nvn_bridge_texture_copy(&guest,&value));
        assert(value.serial==current.serial && value.mem==backing+64);
    }
    return NULL;
}
int main(void) {
    current.memory_pool=&pool_guest;current.memory_offset=64;current.d.format=1;current.d.width=16;
    ffrs_tex value,first;
    assert(!nvn_bridge_texture_copy(NULL,&value));assert(!nvn_bridge_texture_copy(&guest,NULL));
    for(uint64_t i=1;i<=100000;i++) {
        current.serial=i;assert(nvn_bridge_texture_copy(&guest,&value));
        assert(value.serial==i && value.mem==backing+64 && value.L.size==64);
        if(i==1)first=value;
    }
    assert(texture_allocations==1);assert(first.serial==1); /* caller's old value unchanged */
    const ffrs_tex *command=nvn_bridge_texture(&guest);assert(command && command->serial==100000);
    ffrs_tex saved=*command;
    current.serial++;current.d.width=32;
    assert(nvn_bridge_texture_copy(&guest,&value));assert(texture_allocations==2);
    assert(value.serial==100001 && value.L.size==128);assert(!memcmp(command,&saved,sizeof(saved)));
    /* End the reusable generation using a value, never a pointer that can be overwritten. */
    nvn_bridge_texture_end(&guest,100001);assert(retirements==1 && retired.serial==100001);
    nvn_bridge_texture_end(&guest,99999);assert(retirements==1);
    current.serial++;assert(nvn_bridge_texture_copy(&guest,&value));assert(texture_allocations==2);
    assert(retired.serial==100001 && !memcmp(command,&saved,sizeof(saved)));
    pthread_t threads[8];
    for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,reader,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(texture_allocations==2);
    const ffrs_tex *latest=nvn_bridge_texture(&guest);assert(latest==nvn_bridge_texture(&guest));
    current.serial++;assert(nvn_bridge_texture_copy(&guest,&value));assert(texture_allocations==3);
    assert(latest->serial==100002 && command->serial==100000);
    available=0;assert(!nvn_bridge_texture_copy(&guest,&value));assert(!nvn_bridge_texture(&guest));
    puts("texture snapshot ownership PASS:100000 generations/1 reusable allocation; escaped pointers immutable; retirement;8 readers CPU_ONLY");
    return 0;
}
