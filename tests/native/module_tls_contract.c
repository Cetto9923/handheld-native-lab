#include "../../src/runtime/hle/module_tls.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {THREADS=8,INITIALIZED=0x220,TOTAL=0x1019,ITERATIONS=1000};
static unsigned char template[INITIALIZED];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static pthread_key_t unrelated_key;
static _Thread_local unsigned host_canary;
static unsigned ready,release_workers;
struct result {unsigned id,errors;uintptr_t anchor;size_t span;uintptr_t before[2],after[2];};
static void read_tp(uintptr_t values[2]) {
#if defined(__aarch64__)
    __asm__ volatile("mrs %0, tpidr_el0":"=r"(values[0]));
    __asm__ volatile("mrs %0, tpidrro_el0":"=r"(values[1]));
#else
    values[0]=values[1]=0;
#endif
}
static unsigned verify(const nso_module_tls_view *view,const nso_module_tls_template *source) {
    unsigned errors=0;
    if(view->initialized_size!=source->initialized_size || view->total_size!=source->total_size ||
       view->alignment!=source->alignment || view->reserved_header_size!=source->reserved_header_size ||
       view->storage_size<source->reserved_header_size+source->total_size ||
       view->storage_size%source->alignment ||
       view->payload!=view->anchor+source->reserved_header_size ||
       (uintptr_t)view->payload%source->alignment)errors++;
    for(size_t i=0;i<source->reserved_header_size;i++)if(view->anchor[i])errors++;
    if(source->initialized_size && memcmp(view->payload,source->initialized_data,source->initialized_size))errors++;
    for(size_t i=source->initialized_size;i<source->total_size;i++)if(view->payload[i])errors++;
    for(size_t i=source->reserved_header_size+source->total_size;i<view->storage_size;i++)
        if(view->anchor[i])errors++;
    return errors;
}
static unsigned basic_templates(void) {
    unsigned errors=0;
    const unsigned char bytes[]={0x91,0x22,0xe7,0x04,0x55,0xff,0x01,0x82,0x13,0x94,0x25,0xb6,0x47};
    nso_module_tls_template cases[]={
        {NULL,0,0,8,16}, {NULL,0,0,1,0}, {bytes,3,3,1,16},
        {bytes,sizeof(bytes),37,64,16}, {NULL,0,257,256,16}
    };
    const size_t spans[]={16,0,19,64,512};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        nso_module_tls_image *image=NULL;
        if(nso_module_tls_create(&cases[i],&image)) {errors++;continue;}
        errors+=verify(nso_module_tls_get_view(image),&cases[i]);
        if(nso_module_tls_get_view(image)->storage_size!=spans[i])errors++;
        nso_module_tls_free(image);
    }
    unsigned char *temporary=malloc(INITIALIZED);
    if(!temporary)return errors+1;
    memcpy(temporary,template,INITIALIZED);
    nso_module_tls_template source={temporary,INITIALIZED,TOTAL,8,16};
    nso_module_tls_image *image=NULL;
    int result=nso_module_tls_create(&source,&image);
    /* The component must own its copy after the original buffer is released. */
    memset(temporary,0x5a,INITIALIZED);free(temporary);
    if(result)return errors+1;
    const nso_module_tls_view *view=nso_module_tls_get_view(image);
    source.initialized_data=template;
    errors+=verify(view,&source);
    if(view->storage_size!=0x1030 || view->payload-view->anchor!=16)errors++;
    nso_module_tls_free(image);
    return errors;
}
static void *worker(void *argument) {
    struct result *r=argument;
    host_canary=0x9e370000u+r->id;
    if(pthread_setspecific(unrelated_key,r))r->errors++;
    read_tp(r->before);
    nso_module_tls_template source={template,INITIALIZED,TOTAL,8,16};
    nso_module_tls_image *image=NULL;
    if(nso_module_tls_create(&source,&image))r->errors++;
    const nso_module_tls_view *view=nso_module_tls_get_view(image);
    if(view) {
        r->errors+=verify(view,&source);
        r->anchor=(uintptr_t)view->anchor;r->span=view->storage_size;
        if(view->storage_size!=0x1030)r->errors++;
        view->payload[0]=(unsigned char)r->id;
        view->payload[INITIALIZED-1]=(unsigned char)(r->id+16);
        view->payload[INITIALIZED]=(unsigned char)(r->id+32);
        view->payload[TOTAL-1]=(unsigned char)(r->id+48);
    }
    pthread_mutex_lock(&mutex);ready++;pthread_cond_broadcast(&condition);
    while(!release_workers)pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    if(view)for(unsigned n=0;n<ITERATIONS;n++)for(size_t i=0;i<TOTAL;i++) {
        unsigned char expected=i<INITIALIZED?template[i]:0;
        if(i==0)expected=(unsigned char)r->id;
        else if(i==INITIALIZED-1)expected=(unsigned char)(r->id+16);
        else if(i==INITIALIZED)expected=(unsigned char)(r->id+32);
        else if(i==TOTAL-1)expected=(unsigned char)(r->id+48);
        if(view->payload[i]!=expected)r->errors++;
    }
    nso_module_tls_free(image);
    read_tp(r->after);
    /* EL0 may vary with macOS scheduling; observe it, as in the existing PoC. */
    if(r->before[1]!=r->after[1] || pthread_getspecific(unrelated_key)!=r ||
       host_canary!=0x9e370000u+r->id)r->errors++;
    return NULL;
}
int main(void) {
    for(size_t i=0;i<INITIALIZED;i++)template[i]=(unsigned char)(i*37+11);
    unsigned char original[INITIALIZED];memcpy(original,template,sizeof(original));
    unsigned errors=basic_templates(),el0_changes=0;
    pthread_t threads[THREADS];struct result results[THREADS]={0};
    if(pthread_key_create(&unrelated_key,NULL))return 1;
    for(unsigned i=0;i<THREADS;i++) {
        results[i].id=i+1;
        if(pthread_create(&threads[i],NULL,worker,&results[i]))return 1;
    }
    pthread_mutex_lock(&mutex);
    while(ready!=THREADS)pthread_cond_wait(&condition,&mutex);
    for(unsigned i=0;i<THREADS;i++)for(unsigned j=0;j<i;j++) {
        uintptr_t a=results[i].anchor,b=results[j].anchor;
        if(a<b+results[j].span && b<a+results[i].span)errors++;
    }
    release_workers=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&mutex);
    for(unsigned i=0;i<THREADS;i++) {
        if(pthread_join(threads[i],NULL))return 1;
        errors+=results[i].errors;
        if(results[i].before[0]!=results[i].after[0])el0_changes++;
        printf("thread=%u anchor=0x%" PRIxPTR " span=%zu errors=%u host_ro_unchanged=%d\n",
            results[i].id,results[i].anchor,results[i].span,results[i].errors,
            results[i].before[1]==results[i].after[1]);
    }
    if(memcmp(original,template,sizeof(original)))errors++;
    pthread_key_delete(unrelated_key);
    printf("{\"threads\":8,\"initialized\":544,\"total\":4121,\"alignment\":8,"
           "\"header\":16,\"storage\":4144,\"payload_value_checks\":%u,\"errors\":%u,"
           "\"tpidr_el0_observed_changes\":%u,\"sdk_tls_bound\":false}\n",
           THREADS*TOTAL*ITERATIONS,errors,el0_changes);
    return errors?1:0;
}
