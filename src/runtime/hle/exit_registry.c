#include "exit_registry.h"
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct callback {
    enum {PLAIN,CXA} kind;
    union {nso_atexit_callback plain;nso_cxa_callback cxa;} fn;
    void *arg,*dso;
    struct callback *next;
};
struct nso_exit_registry {
    pthread_mutex_t mutex,finalize_mutex;
    struct callback *head;
    size_t pending;
};
nso_exit_registry *nso_exit_registry_create(void) {
    nso_exit_registry *r=calloc(1,sizeof(*r));
    if(!r)return NULL;
    int error=pthread_mutex_init(&r->mutex,NULL);
    if(error){free(r);return NULL;}
    pthread_mutexattr_t attr;
    error=pthread_mutexattr_init(&attr);
    if(error){pthread_mutex_destroy(&r->mutex);free(r);return NULL;}
    error=pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    if(!error)error=pthread_mutex_init(&r->finalize_mutex,&attr);
    pthread_mutexattr_destroy(&attr);
    if(error){pthread_mutex_destroy(&r->mutex);free(r);return NULL;}
    return r;
}
static int add(nso_exit_registry *r,struct callback *node) {
    if(!r || !node){free(node);return -1;}
    pthread_mutex_lock(&r->mutex);
    node->next=r->head;r->head=node;r->pending++;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int nso_exit_registry_atexit(nso_exit_registry *r,nso_atexit_callback fn) {
    if(!fn)return -1;
    struct callback *n=malloc(sizeof(*n));
    if(!n)return -1;
    *n=(struct callback){.kind=PLAIN,.fn.plain=fn};
    return add(r,n);
}
int nso_exit_registry_cxa_atexit(nso_exit_registry *r,nso_cxa_callback fn,void *arg,void *dso) {
    if(!fn)return -1;
    struct callback *n=malloc(sizeof(*n));
    if(!n)return -1;
    *n=(struct callback){.kind=CXA,.fn.cxa=fn,.arg=arg,.dso=dso};
    return add(r,n);
}
void nso_exit_registry_finalize(nso_exit_registry *r,void *dso) {
    if(!r)return;
    /* Serialize concurrent drains, permit recursive finalize by the same
     * callback. Never hold the list lock while invoking guest code. */
    pthread_mutex_lock(&r->finalize_mutex);
    for(;;) {
        pthread_mutex_lock(&r->mutex);
        struct callback **slot=&r->head;
        while(*slot && dso && ((*slot)->kind!=CXA || (*slot)->dso!=dso))slot=&(*slot)->next;
        struct callback *n=*slot;
        if(!n){pthread_mutex_unlock(&r->mutex);break;}
        *slot=n->next;r->pending--;
        pthread_mutex_unlock(&r->mutex);
        /* Remove before invocation: recursive/repeated finalize cannot call
         * this registration twice. A newly registered callback joins the head. */
        struct callback call=*n;free(n);
        if(call.kind==PLAIN)call.fn.plain();
        else call.fn.cxa(call.arg);
    }
    pthread_mutex_unlock(&r->finalize_mutex);
}
void nso_exit_registry_discard(nso_exit_registry *r) {
    if(!r)return;
    pthread_mutex_lock(&r->finalize_mutex);
    pthread_mutex_lock(&r->mutex);
    struct callback *n=r->head;
    r->head=NULL;r->pending=0;
    pthread_mutex_unlock(&r->mutex);
    while(n){struct callback *next=n->next;free(n);n=next;}
    pthread_mutex_unlock(&r->finalize_mutex);
}
void nso_exit_registry_destroy(nso_exit_registry *r) {
    if(!r)return;
    nso_exit_registry_discard(r);
    pthread_mutex_destroy(&r->finalize_mutex);pthread_mutex_destroy(&r->mutex);free(r);
}
size_t nso_exit_registry_pending(nso_exit_registry *r) {
    if(!r)return 0;
    pthread_mutex_lock(&r->mutex);size_t result=r->pending;pthread_mutex_unlock(&r->mutex);
    return result;
}
static pthread_once_t process_once=PTHREAD_ONCE_INIT;
static nso_exit_registry *process_registry;
static pthread_mutex_t scope_mutex=PTHREAD_MUTEX_INITIALIZER;
static void *module_scope;
static void create_process_registry(void) {process_registry=nso_exit_registry_create();}
static nso_exit_registry *current_process(void) {
    if(pthread_once(&process_once,create_process_registry))return NULL;
    return process_registry;
}
int nso_host_atexit(nso_atexit_callback fn) {
    nso_exit_registry *r=current_process();
    int result=nso_exit_registry_atexit(r,fn);
    if(!result)fprintf(stderr,"[crt] atexit registered callback=0x%" PRIxPTR " pending=%zu; deferred\n",
                      (uintptr_t)fn,nso_exit_registry_pending(r));
    return result;
}
int nso_host_cxa_atexit(nso_cxa_callback fn,void *arg,void *dso) {
    return nso_exit_registry_cxa_atexit(current_process(),fn,arg,dso);
}
void nso_host_cxa_finalize(void *dso) {nso_exit_registry_finalize(current_process(),dso);}
void nso_host_discard_exit_callbacks(void) {nso_exit_registry_discard(process_registry);}
int nso_host_exit_scope_begin(void *owner) {
    if(!owner)return -1;
    pthread_mutex_lock(&scope_mutex);
    nso_exit_registry *registry=current_process();
    if(!registry || module_scope || nso_exit_registry_pending(registry)) {
        pthread_mutex_unlock(&scope_mutex);return -1;
    }
    module_scope=owner;
    pthread_mutex_unlock(&scope_mutex);
    return 0;
}
void nso_host_exit_scope_end(void *owner) {
    /* The caller must have joined guest threads before unloading code. */
    pthread_mutex_lock(&scope_mutex);
    if(module_scope!=owner) {
        pthread_mutex_unlock(&scope_mutex);
        fprintf(stderr,"[crt] mismatched module unload scope\n");abort();
    }
    nso_host_discard_exit_callbacks();
    module_scope=NULL;
    pthread_mutex_unlock(&scope_mutex);
}
_Noreturn void nso_host_exit(int status) {
    nso_host_cxa_finalize(NULL);
    exit(status);
}
_Noreturn void nso_host_immediate_exit(int status) {_Exit(status);}
