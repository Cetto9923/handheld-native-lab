/* Fixed native adapter for MRS Xt,TPIDR_EL0, not a general CPU translator.
 * No C call or host TP/x18 substitution occurs in generated code. Each thunk
 * preserves x14-x17 in a 32-byte guest stack frame, reads an acquire-published
 * (host TP, guest TP) pair, writes only the instruction's destination, and
 * branches back. It uses no flag-setting or SIMD instructions. A table miss
 * restores the original state before the existing BRK adapter handles it.
 *
 * RX blocks are immutable process-lifetime owners. Like registered guest text
 * ranges they cannot be reclaimed while any loaded module may branch to them.
 * Mapping/protection/reach failures leave the existing signal path in place.
 */
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#ifndef GUEST_TP_FAST_CAPACITY
#define GUEST_TP_FAST_CAPACITY 128
#endif
_Static_assert(GUEST_TP_FAST_CAPACITY>0 && GUEST_TP_FAST_CAPACITY<=65535,"TP slot counter encoding");
typedef struct {uintptr_t owner,base;} guest_tp_fast_entry;
_Static_assert(sizeof(guest_tp_fast_entry)==16,"TP table native stride");
static guest_tp_fast_entry guest_tp_fast_entries[GUEST_TP_FAST_CAPACITY];
static pthread_mutex_t guest_tp_fast_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int guest_tp_fast_slot=-1;
static uintptr_t guest_tp_fast_host_tp(void){uintptr_t p;__asm__ volatile("mrs %0,tpidr_el0":"=r"(p));return p;}
static void guest_tp_fast_bind(const unsigned char *base){
    uintptr_t tp=guest_tp_fast_host_tp();
    pthread_mutex_lock(&guest_tp_fast_lock);
    for(unsigned i=0;i<GUEST_TP_FAST_CAPACITY;i++)if(!__atomic_load_n(&guest_tp_fast_entries[i].owner,__ATOMIC_RELAXED)){
        __atomic_store_n(&guest_tp_fast_entries[i].base,(uintptr_t)base,__ATOMIC_RELAXED);
        __atomic_store_n(&guest_tp_fast_entries[i].owner,tp,__ATOMIC_RELEASE);guest_tp_fast_slot=(int)i;break;
    }
    pthread_mutex_unlock(&guest_tp_fast_lock);
    /* Capacity affects speed only: a bound thread without a slot uses BRK. */
}
static void guest_tp_fast_unbind(void){
    if(guest_tp_fast_slot<0)return;
    pthread_mutex_lock(&guest_tp_fast_lock);
    guest_tp_fast_entry *entry=&guest_tp_fast_entries[guest_tp_fast_slot];
    if(__atomic_load_n(&entry->owner,__ATOMIC_RELAXED)!=guest_tp_fast_host_tp())abort();
    __atomic_store_n(&entry->owner,0,__ATOMIC_RELEASE);
    __atomic_store_n(&entry->base,0,__ATOMIC_RELAXED);guest_tp_fast_slot=-1;
    pthread_mutex_unlock(&guest_tp_fast_lock);
}
enum {GUEST_TP_FAST_WORDS=28,GUEST_TP_FAST_BYTES=112};
/* Assembled and device-checked template; fixed local branches, literal at104.
 * Patched words: 5=capacity,15=destination,19/24=return,23=BRK,26/27=table.
 * The literal load at16 is LDR x17,[PC+88]. */
static const uint32_t guest_tp_fast_template[GUEST_TP_FAST_WORDS]={
    0xd10083ff,0xa9003fee,0xa90147f0,0xd53bd050,0x580002d1,0xd280100e,
    0xc8dffe2f,0xca1001ef,0xb40000af,0x91004231,0xd10005ce,0xb5ffff6e,
    0x14000008,0xf940062f,0xb40000cf,0xd503201f,0xa9403fee,0xa94147f0,
    0x910083ff,0x00000000,0xa9403fee,0xa94147f0,0x910083ff,0xd4254000,
    0x00000000,0xd503201f,0,0
};
typedef struct guest_tp_fast_block{unsigned char *code;size_t bytes;struct guest_tp_fast_block *next;}guest_tp_fast_block;
static guest_tp_fast_block *guest_tp_fast_blocks;
static int guest_tp_fast_contains(uintptr_t pc){
    for(guest_tp_fast_block *b=__atomic_load_n(&guest_tp_fast_blocks,__ATOMIC_ACQUIRE);b;b=b->next)
        if(pc>=(uintptr_t)b->code && pc-(uintptr_t)b->code<b->bytes)return 1;
    return 0;
}
static int guest_tp_fast_branch(uintptr_t from,uintptr_t to,uint32_t *out){
    if((from|to)&3)return 0;
    int64_t delta;
    if(to>=from){uintptr_t d=to-from;if(d>=((uintptr_t)1<<27))return 0;delta=(int64_t)d;}
    else{uintptr_t d=from-to;if(d>((uintptr_t)1<<27))return 0;delta=-(int64_t)d;}
    *out=0x14000000u|((uint32_t)(delta/4)&0x3ffffffu);return 1;
}
static int guest_tp_fast_make(uint32_t *code,uintptr_t site,unsigned reg){
    uint32_t enter,ret,miss;
    if(!guest_tp_fast_branch(site,(uintptr_t)code,&enter) ||
       !guest_tp_fast_branch((uintptr_t)(code+19),site+4,&ret) ||
       !guest_tp_fast_branch((uintptr_t)(code+24),site+4,&miss))return 0;
    memcpy(code,guest_tp_fast_template,sizeof(guest_tp_fast_template));
    code[5]=0xd280000e|((uint32_t)GUEST_TP_FAST_CAPACITY<<5);
    code[15]=reg==31?0xd503201f:reg>=14&&reg<=17?0xf90003ef|((reg-14)<<10):0xaa0f03e0|reg;
    code[19]=ret;code[23]=guest_tp_trap_encode(reg);code[24]=miss;
    uintptr_t table=(uintptr_t)guest_tp_fast_entries;memcpy(code+26,&table,8);return 1;
}
static guest_tp_fast_block *guest_tp_fast_prepare(const uint32_t *text,size_t bytes){
    const char *option=getenv("FFRS_GUEST_TP_FAST");if(option && option[0]=='0')return NULL;
    size_t sites=0;for(size_t i=0;i<bytes/4;i++)sites+=(text[i]&~31u)==GUEST_TP_MRS_TPIDR_EL0;
    if(!sites || sites>64u*1024u*1024u/GUEST_TP_FAST_BYTES)return NULL;
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return NULL;
    size_t size=sites*GUEST_TP_FAST_BYTES;size=(size+(size_t)page-1)/(size_t)page*(size_t)page;
    if(bytes>UINTPTR_MAX-(uintptr_t)text)return NULL;
    uintptr_t center=((uintptr_t)text+bytes/2)/(uintptr_t)page*(uintptr_t)page;
    unsigned char *mapping=NULL;
    for(unsigned step=1;step<8 && !mapping;step++)for(unsigned side=0;side<2;side++){
        uintptr_t distance=(uintptr_t)step*16u*1024u*1024u;
        if((side && center<distance)||(!side && center>UINTPTR_MAX-distance))continue;
        uintptr_t hint=side?center-distance:center+distance;
        void *p=mmap((void*)hint,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED)continue;
        uint32_t unused;if(guest_tp_fast_branch(center,(uintptr_t)p,&unused))mapping=p;else munmap(p,size);
        if(mapping)break;
    }
    if(!mapping)return NULL;
    guest_tp_fast_block *block=malloc(sizeof(*block));if(!block){munmap(mapping,size);return NULL;}
    memset(mapping,0,size);size_t index=0;
    for(size_t i=0;i<bytes/4;i++)if((text[i]&~31u)==GUEST_TP_MRS_TPIDR_EL0){
        guest_tp_fast_make((uint32_t*)(mapping+index*GUEST_TP_FAST_BYTES),(uintptr_t)&text[i],text[i]&31u);index++;
    }
    __builtin___clear_cache((char*)mapping,(char*)mapping+size);
    if(mprotect(mapping,size,PROT_READ|PROT_EXEC)){munmap(mapping,size);free(block);return NULL;}
    block->code=mapping;block->bytes=size;
    do{block->next=__atomic_load_n(&guest_tp_fast_blocks,__ATOMIC_ACQUIRE);}
    while(!__atomic_compare_exchange_n(&guest_tp_fast_blocks,&block->next,block,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE));
    return block;
}
