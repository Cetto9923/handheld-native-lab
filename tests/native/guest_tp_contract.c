#include "../../src/runtime/hle/guest_tp.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {THREADS=8,INITIALIZED=0x220,TOTAL=0x1019,HEADER=0x10,ALIGNMENT=8,STORAGE=0x1030};
enum {BARRIER_ROUNDS=64};

static unsigned char template_bytes[INITIALIZED];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static unsigned ready,release_workers;

/* Real instruction words observed in the reviewed main module text. Each
 * window is the nine instructions that end at the MRS, so the MRS is always
 * the fifth word. The module address of the MRS and the guest TP offset that
 * the window builds are recorded, so a later run can match them against the
 * module bytes again. */
enum {OBSERVED_WINDOW=9,OBSERVED_MRS=4};
struct observed_site { unsigned mrs_address; unsigned tp_offset; uint32_t words[OBSERVED_WINDOW]; };
static const struct observed_site observed[] = {
    {0x17040,0x240,{0xd2a00000u,0xf2804800u,0xd503201fu,0xd503201fu,0xd53bd048u,
                     0x38606908u,0x340008c8u,0x94002c43u,0xb9402008u}},
    {0x34bc,0x230,{0xd2a00000u,0xf2804600u,0xd503201fu,0xd503201fu,0xd53bd048u,
                    0x8b000113u,0xaa1303e0u,0x94007bdau,0xaa1303e1u}},
    {0x30338,0x270,{0xd2a00000u,0xf2804e00u,0xd503201fu,0xd503201fu,0xd53bd054u,
                     0x38606a88u,0x36000148u,0xd2a00000u,0xf2804d00u}},
    {0x5209348,0x1028,{0xd2a00000u,0xf2820500u,0xd503201fu,0xd503201fu,0xd53bd048u,
                        0x38606908u,0x340004c8u,0xb9418ac8u,0xf0008749u}},
    {0xb858c,0x10,{0xd2a00000u,0xf2800200u,0xd503201fu,0xd503201fu,0xd53bd048u,
                    0xb8606908u,0x7100091fu,0x54000240u,0x7100051fu}}
};

static unsigned mirror_model(void) {
    unsigned errors=0;
    for(size_t i=0;i<INITIALIZED;i++)template_bytes[i]=(unsigned char)(i*37+11);
    unsigned char original[INITIALIZED];
    memcpy(original,template_bytes,sizeof(original));
    guest_tp_template source={template_bytes,INITIALIZED,TOTAL,ALIGNMENT,HEADER};
    guest_tp_mirror *mirror=NULL;
    struct guest_tp_result result=guest_tp_mirror_create(&source,&mirror);
    if(result.status!=GUEST_TP_OK||!mirror){errors++;return errors;}
    const unsigned char *base=guest_tp_mirror_base(mirror);
    const unsigned char *payload=guest_tp_mirror_payload(mirror);
    if(!base||!payload||payload!=base+HEADER)errors++;
    if(guest_tp_mirror_span(mirror)!=STORAGE)errors++;
    if((uintptr_t)base%ALIGNMENT)errors++;
    for(size_t i=0;i<HEADER;i++)if(base[i])errors++;
    if(memcmp(payload,template_bytes,INITIALIZED))errors++;
    /* The template tail must be zero, never copied from module data. */
    for(size_t i=INITIALIZED;i<TOTAL;i++)if(payload[i])errors++;
    for(size_t i=HEADER+TOTAL;i<STORAGE;i++)if(base[i])errors++;
    /* The mirror owns its copy; the source may change or disappear. */
    memset(template_bytes,0x5a,INITIALIZED);
    if(memcmp(payload,original,INITIALIZED))errors++;
    memset(original,0,sizeof(original));
    guest_tp_mirror_free(mirror);
    /* Invalid templates are refused and never allocate. */
    guest_tp_template bad[]={
        {NULL,0,0,0,0},
        {NULL,0,0,ALIGNMENT*3,HEADER},
        {template_bytes,TOTAL+1,TOTAL,ALIGNMENT,HEADER},
        {NULL,INITIALIZED,TOTAL,ALIGNMENT,HEADER}
    };
    size_t live_before=guest_tp_live_mirrors();
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        guest_tp_mirror *rejected=NULL;
        struct guest_tp_result refused=guest_tp_mirror_create(&bad[i],&rejected);
        if(refused.status!=GUEST_TP_INVALID||rejected)errors++;
        if(rejected)guest_tp_mirror_free(rejected);
    }
    if(guest_tp_live_mirrors()!=live_before)errors++;
    /* An empty template is valid: only the reserved header exists. */
    guest_tp_template empty={NULL,0,0,ALIGNMENT,HEADER};
    guest_tp_mirror *blank=NULL;
    struct guest_tp_result made=guest_tp_mirror_create(&empty,&blank);
    if(made.status!=GUEST_TP_OK||!blank)errors++;
    else {
        const unsigned char *blank_base=guest_tp_mirror_base(blank);
        if(!blank_base||guest_tp_mirror_span(blank)!=HEADER)errors++;
        if(guest_tp_mirror_payload(blank)!=blank_base+HEADER)errors++;
        for(size_t i=0;i<HEADER;i++)if(blank_base[i])errors++;
        guest_tp_mirror_free(blank);
    }
    if(guest_tp_mirror_create(&source,NULL).status!=GUEST_TP_INVALID)errors++;
    return errors;
}

static unsigned patch_model(void) {
    unsigned errors=0;
    uint32_t words[32];
    memset(words,0,sizeof(words));
    words[0]=0xd53bd048u;      /* MRS x8 */
    words[1]=0xd53bd054u;      /* MRS x20 */
    words[2]=0xd53bd060u;      /* MRS TPIDRRO_EL0: never rewritten */
    words[3]=0xd51bd040u;      /* MSR TPIDR_EL0: never written */
    words[4]=0x38606908u;      /* LDRB w8,[x8,#0x240] */
    words[5]=0xd2a00000u;      /* MOVZ x0,#0 */
    words[6]=0x1402cf1au;      /* B */
    struct guest_tp_patch_report report;
    struct guest_tp_result result=guest_tp_scan_text(words,sizeof(words),&report);
    if(result.status!=GUEST_TP_OK)errors++;
    if(report.scanned_words!=sizeof(words)/sizeof(*words)||report.patched)errors++;
    if(report.registers[8]!=1||report.registers[20]!=1)errors++;
    if(report.tpidrro_sites!=1||report.msr_sites!=1)errors++;
    result=guest_tp_patch_text(words,sizeof(words),&report);
    if(result.status!=GUEST_TP_OK||report.patched!=2)errors++;
    if(report.tpidrro_sites!=1||report.msr_sites!=1)errors++;
    if(words[0]!=guest_tp_trap_encode(8)||words[1]!=guest_tp_trap_encode(20))errors++;
    if(words[2]!=0xd53bd060u||words[3]!=0xd51bd040u||words[4]!=0x38606908u)errors++;
    if(words[5]!=0xd2a00000u||words[6]!=0x1402cf1au)errors++;
    /* Patching twice changes nothing: a rewritten word is no longer an MRS. */
    result=guest_tp_patch_text(words,sizeof(words),&report);
    if(result.status!=GUEST_TP_OK||report.patched!=0)errors++;
    if(words[0]!=guest_tp_trap_encode(8)||words[1]!=guest_tp_trap_encode(20))errors++;
    /* A site that is not an MRS read is refused. */
    result=guest_tp_patch_site(&words[2],&report);
    if(result.status!=GUEST_TP_INVALID||report.patched)errors++;
    result=guest_tp_patch_site(&words[0],&report);
    if(result.status!=GUEST_TP_INVALID||report.patched)errors++;
    /* Every destination register survives the rewrite. */
    for(unsigned reg=0;reg<GUEST_TP_REGISTER_COUNT;reg++) {
        uint32_t site=GUEST_TP_MRS_TPIDR_EL0|reg;
        uint32_t encoded=guest_tp_trap_encode(reg);
        unsigned decoded=GUEST_TP_REGISTER_COUNT;
        if(!guest_tp_trap_register(encoded,&decoded)||decoded!=reg)errors++;
        if(guest_tp_trap_register(site,&decoded))errors++;
        if(guest_tp_trap_encode(GUEST_TP_REGISTER_COUNT)!=0)errors++;
    }
    return errors;
}

static unsigned observed_model(void) {
    unsigned errors=0;
    for(size_t i=0;i<sizeof(observed)/sizeof(*observed);i++) {
        uint32_t words[OBSERVED_WINDOW];
        memcpy(words,observed[i].words,sizeof(words));
        /* The offset register keeps the guest TP offset; the rewrite must
         * only replace the MRS word and keep its destination register. */
        unsigned site=OBSERVED_MRS;
        unsigned expected_destination=observed[i].words[site]&31u;
        struct guest_tp_patch_report report;
        struct guest_tp_result result=guest_tp_patch_site(&words[site],&report);
        if(result.status!=GUEST_TP_OK)errors++;
        if(report.patched!=1)errors++;
        if(words[site]!=guest_tp_trap_encode(expected_destination))errors++;
        unsigned decoded=GUEST_TP_REGISTER_COUNT;
        if(!guest_tp_trap_register(words[site],&decoded)||decoded!=expected_destination)errors++;
        for(unsigned word=0;word<OBSERVED_WINDOW;word++) {
            if(word==site)continue;
            if(words[word]!=observed[i].words[word])errors++;
        }
    }
    return errors;
}

static unsigned trap_decision_model(void) {
    unsigned errors=0;
    const unsigned char base[4]={0x11,0x22,0x33,0x44};
    uint32_t trap=guest_tp_trap_encode(8);
    unsigned destination=GUEST_TP_REGISTER_COUNT;
    int advance=0;
    /* The kernel left the PC on the trap. */
    enum guest_tp_trap_decision decision=guest_tp_trap_decide(1,trap,0xd503201fu,base,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_RESOLVE||destination!=8||!advance)errors++;
    /* The kernel advanced the PC past the trap. */
    decision=guest_tp_trap_decide(1,0xd503201fu,trap,base,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_RESOLVE||destination!=8||advance)errors++;
    /* Two trap words next to each other: the word at the PC wins, so the
     * trap is resolved once and the PC moves past it instead of looping. */
    decision=guest_tp_trap_decide(1,guest_tp_trap_encode(9),trap,base,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_RESOLVE||destination!=9||!advance)errors++;
    /* A trap outside a registered range is not ours. */
    decision=guest_tp_trap_decide(0,trap,0xd503201fu,base,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_CHAIN)errors++;
    /* A plain BRK inside a registered range is not ours either. */
    decision=guest_tp_trap_decide(1,0xd4200000u,0xd503201fu,base,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_CHAIN)errors++;
    /* An adapter trap without a mirror stays observable. */
    decision=guest_tp_trap_decide(1,trap,0xd503201fu,NULL,&destination,&advance);
    if(decision!=GUEST_TP_TRAP_NO_MIRROR)errors++;
    uint64_t value=0;
    struct guest_tp_result resolved=guest_tp_resolve_trap(trap,base,&destination,&value);
    if(resolved.status!=GUEST_TP_OK||destination!=8||value!=(uint64_t)(uintptr_t)base)errors++;
    resolved=guest_tp_resolve_trap(trap,NULL,&destination,&value);
    if(resolved.status!=GUEST_TP_UNBOUND)errors++;
    resolved=guest_tp_resolve_trap(0xd4200000u,base,&destination,&value);
    if(resolved.status!=GUEST_TP_INVALID)errors++;
    return errors;
}

struct worker_result {unsigned id,errors;uintptr_t base,span;};
static void *worker(void *argument) {
    struct worker_result *result=argument;
    guest_tp_template source={template_bytes,INITIALIZED,TOTAL,ALIGNMENT,HEADER};
    guest_tp_mirror *mirror=NULL;
    struct guest_tp_result created=guest_tp_mirror_create(&source,&mirror);
    if(created.status!=GUEST_TP_OK||!mirror) {result->errors++;return NULL;}
    struct guest_tp_result bound=guest_tp_bind_current(mirror);
    if(bound.status!=GUEST_TP_OK) {result->errors++;guest_tp_mirror_free(mirror);return NULL;}
    const unsigned char *base=guest_tp_current_base();
    if(base!=guest_tp_mirror_base(mirror))result->errors++;
    /* A second bind is refused and keeps the first mirror. */
    guest_tp_mirror *second=NULL;
    if(guest_tp_mirror_create(&source,&second).status!=GUEST_TP_OK)result->errors++;
    if(guest_tp_bind_current(second).status!=GUEST_TP_BUSY)result->errors++;
    guest_tp_mirror_free(second);
    result->base=(uintptr_t)base;
    result->span=guest_tp_mirror_span(mirror);
    if(result->span!=STORAGE)result->errors++;
    guest_tp_mirror_payload(mirror)[0]=(unsigned char)(result->id+1);
    guest_tp_mirror_payload(mirror)[INITIALIZED-1]=(unsigned char)(result->id+2);
    guest_tp_mirror_payload(mirror)[INITIALIZED]=(unsigned char)(result->id+3);
    guest_tp_mirror_payload(mirror)[TOTAL-1]=(unsigned char)(result->id+4);
    pthread_mutex_lock(&mutex);ready++;pthread_cond_broadcast(&condition);
    while(!release_workers)pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    for(unsigned round=0;round<BARRIER_ROUNDS;round++) {
        const unsigned char *payload=guest_tp_mirror_payload(mirror);
        if(payload[0]!=(unsigned char)(result->id+1))result->errors++;
        if(payload[INITIALIZED-1]!=(unsigned char)(result->id+2))result->errors++;
        if(payload[INITIALIZED]!=(unsigned char)(result->id+3))result->errors++;
        if(payload[TOTAL-1]!=(unsigned char)(result->id+4))result->errors++;
        if(guest_tp_current_base()!=base)result->errors++;
    }
    struct guest_tp_result unbound=guest_tp_unbind_current();
    if(unbound.status!=GUEST_TP_OK)result->errors++;
    if(guest_tp_current_base())result->errors++;
    if(guest_tp_unbind_current().status!=GUEST_TP_UNBOUND)result->errors++;
    return NULL;
}

/* A thread that exits while holding a mirror must release it. */
static void *exiting_worker(void *argument) {
    struct worker_result *result=argument;
    guest_tp_template source={template_bytes,INITIALIZED,TOTAL,ALIGNMENT,HEADER};
    guest_tp_mirror *mirror=NULL;
    if(guest_tp_mirror_create(&source,&mirror).status!=GUEST_TP_OK){result->errors++;return NULL;}
    if(guest_tp_bind_current(mirror).status!=GUEST_TP_OK){result->errors++;return NULL;}
    result->base=(uintptr_t)guest_tp_current_base();
    return NULL;
}

int main(void) {
    for(size_t i=0;i<INITIALIZED;i++)template_bytes[i]=(unsigned char)(i*37+11);
    unsigned e_mirror=mirror_model(),e_patch=patch_model(),e_observed=observed_model(),e_trap=trap_decision_model();
    unsigned errors=e_mirror+e_patch+e_observed+e_trap;
    pthread_t threads[THREADS];struct worker_result results[THREADS];
    memset(results,0,sizeof(results));
    for(unsigned i=0;i<THREADS;i++) {
        results[i].id=i+1;
        if(pthread_create(&threads[i],NULL,worker,&results[i]))return 1;
    }
    pthread_mutex_lock(&mutex);
    while(ready!=THREADS)pthread_cond_wait(&condition,&mutex);
    for(unsigned i=0;i<THREADS;i++)for(unsigned j=0;j<i;j++) {
        uintptr_t a=results[i].base,b=results[j].base;
        if(a<b+results[j].span&&b<a+results[i].span)errors++;
    }
    release_workers=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&mutex);
    for(unsigned i=0;i<THREADS;i++) {
        if(pthread_join(threads[i],NULL))return 1;
        errors+=results[i].errors;
    }
    /* Thread exit cleanup. */
    size_t live_before=guest_tp_live_mirrors();
    struct worker_result exiting;
    memset(&exiting,0,sizeof(exiting));
    pthread_t exit_thread;
    if(pthread_create(&exit_thread,NULL,exiting_worker,&exiting))return 1;
    if(pthread_join(exit_thread,NULL))return 1;
    errors+=exiting.errors;
    if(guest_tp_live_mirrors()!=live_before)errors++;
    printf("{\"threads\":%u,\"initialized\":%u,\"total\":%u,\"header\":%u,\"storage\":%u,"
           "\"payload_checks\":%u,\"observed_sites\":%zu,\"live_mirrors\":%zu,"
           "\"trap_handler\":\"NOT_RUN_HOST\",\"mirror_errors\":%u,\"patch_errors\":%u,"
           "\"observed_errors\":%u,\"trap_errors\":%u,\"errors\":%u}\n",
           THREADS,INITIALIZED,TOTAL,HEADER,STORAGE,
           (unsigned)(THREADS*BARRIER_ROUNDS*4),sizeof(observed)/sizeof(*observed),
           guest_tp_live_mirrors(),e_mirror,e_patch,e_observed,e_trap,errors);
    return errors?1:0;
}
