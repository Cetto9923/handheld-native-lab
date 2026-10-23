#define _GNU_SOURCE
#include "mutex.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct thread_identity {void *token;size_t token_size;uint32_t handle;};
struct mutex_entry {
    void *object;
    pthread_mutex_t native;
    uint32_t owner_handle,depth,waiters,users;
    uintptr_t owner_token;
    struct mutex_entry *next;
};
static pthread_mutex_t registry_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct mutex_entry *registry;
static pthread_once_t identity_once=PTHREAD_ONCE_INIT;
static pthread_key_t identity_key;
static uint32_t next_handle;
static void fatal(const char *operation,int error) {
    fprintf(stderr,"[mutex-hle] %s failed (%d); unsupported/invalid operation is fatal\n",operation,error);
    abort();
}
static void release_identity(void *value) {
    struct thread_identity *identity=value;
    munmap(identity->token,identity->token_size);
    free(identity);
}
static void create_identity_key(void) {
    int error=pthread_key_create(&identity_key,release_identity);
    if(error)fatal("pthread_key_create",error);
}
static struct thread_identity *current_identity(void) {
    int error=pthread_once(&identity_once,create_identity_key);
    if(error)fatal("pthread_once",error);
    struct thread_identity *identity=pthread_getspecific(identity_key);
    if(identity)return identity;
    identity=calloc(1,sizeof(*identity));
    if(!identity)fatal("identity allocation",ENOMEM);
    identity->token_size=(size_t)sysconf(_SC_PAGESIZE);
    identity->token=mmap(NULL,identity->token_size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(identity->token==MAP_FAILED)fatal("opaque identity mapping",errno);
    pthread_mutex_lock(&registry_mutex);
    /* The SDK critical word reserves bit 30 for contention. */
    if(next_handle>=0x3ffffffe)fatal("identity namespace exhausted",EOVERFLOW);
    identity->handle=++next_handle;
    pthread_mutex_unlock(&registry_mutex);
    error=pthread_setspecific(identity_key,identity);
    if(error)fatal("pthread_setspecific",error);
    return identity;
}
uintptr_t nso_host_mutex_thread_token(void) {return (uintptr_t)current_identity()->token;}
static struct mutex_entry *find(void *object) {
    struct mutex_entry *e=registry;
    while(e && e->object!=object)e=e->next;
    return e;
}
static struct mutex_entry *borrow(void *object) {
    pthread_mutex_lock(&registry_mutex);
    struct mutex_entry *e=find(object);
    if(!e)fatal("unregistered MutexType",EINVAL);
    e->users++;
    pthread_mutex_unlock(&registry_mutex);
    return e;
}
static void mirror(struct mutex_entry *e) {
    /* Exactly the offsets observed in SDK Lock/TryLock/Unlock. Padding and
     * recursive/level state are untouched after initialization. */
    __atomic_store_n((uint32_t*)((unsigned char*)e->object+8),e->depth,__ATOMIC_RELAXED);
    __atomic_store_n((uintptr_t*)((unsigned char*)e->object+16),e->owner_token,__ATOMIC_RELAXED);
    uint32_t critical=e->owner_handle;
    if(critical && e->waiters)critical|=0x40000000;
    __atomic_store_n((uint32_t*)((unsigned char*)e->object+24),critical,__ATOMIC_RELEASE);
}
void nso_host_initialize_mutex(void *object,bool recursive,int level) {
    if(!object || ((uintptr_t)object&7))fatal("unaligned MutexType",EINVAL);
    struct mutex_entry *e=calloc(1,sizeof(*e));
    if(!e)fatal("mutex allocation",ENOMEM);
    pthread_mutexattr_t attr;
    int error=pthread_mutexattr_init(&attr);
    if(error)fatal("pthread_mutexattr_init",error);
    error=pthread_mutexattr_settype(&attr,recursive?PTHREAD_MUTEX_RECURSIVE:PTHREAD_MUTEX_NORMAL);
    if(error)fatal("pthread_mutexattr_settype",error);
    error=pthread_mutex_init(&e->native,&attr);
    pthread_mutexattr_destroy(&attr);
    if(error)fatal("pthread_mutex_init",error);
    e->object=object;
    pthread_mutex_lock(&registry_mutex);
    if(find(object))fatal("duplicate InitializeMutex",EINVAL);
    e->next=registry;registry=e;
    unsigned char *bytes=object;
    bytes[1]=recursive;
    memcpy(bytes+4,&level,4);
    mirror(e);
    bytes[0]=1;
    pthread_mutex_unlock(&registry_mutex);
}
void nso_host_finalize_mutex(void *object) {
    pthread_mutex_lock(&registry_mutex);
    struct mutex_entry **slot=&registry;
    while(*slot && (*slot)->object!=object)slot=&(*slot)->next;
    if(!*slot || (*slot)->users || (*slot)->depth)fatal("FinalizeMutex while active/unregistered",EBUSY);
    struct mutex_entry *e=*slot;
    int error=pthread_mutex_destroy(&e->native);
    if(error)fatal("pthread_mutex_destroy",error);
    /* The SDK FinalizeMutex writes this byte only. */
    *(unsigned char*)object=0;
    *slot=e->next;
    pthread_mutex_unlock(&registry_mutex);
    free(e);
}
static bool acquire(void *object,bool only_try) {
    struct thread_identity *identity=current_identity();
    struct mutex_entry *e=borrow(object);
    pthread_mutex_lock(&registry_mutex);
    bool waiting=!only_try && e->owner_handle!=identity->handle;
    if(waiting) {e->waiters++;mirror(e);}
    pthread_mutex_unlock(&registry_mutex);
    int error=only_try?pthread_mutex_trylock(&e->native):pthread_mutex_lock(&e->native);
    pthread_mutex_lock(&registry_mutex);
    if(waiting)e->waiters--;
    if(!error) {
        if(e->depth && e->owner_handle!=identity->handle)fatal("native mutex ownership mismatch",EINVAL);
        if(e->depth==UINT32_MAX)fatal("recursive depth overflow",EOVERFLOW);
        e->owner_token=(uintptr_t)identity->token;e->owner_handle=identity->handle;e->depth++;
    } else if(!only_try || error!=EBUSY)fatal("native mutex acquire",error);
    mirror(e);e->users--;
    pthread_mutex_unlock(&registry_mutex);
    return error==0;
}
void nso_host_lock_mutex(void *object) {(void)acquire(object,false);}
bool nso_host_try_lock_mutex(void *object) {return acquire(object,true);}
void nso_host_unlock_mutex(void *object) {
    struct thread_identity *identity=current_identity();
    struct mutex_entry *e=borrow(object);
    pthread_mutex_lock(&registry_mutex);
    if(!e->depth || e->owner_handle!=identity->handle)fatal("UnlockMutex without ownership",EPERM);
    if(!--e->depth) {e->owner_token=0;e->owner_handle=0;}
    mirror(e);
    /* Unlock inside registry scope so a new owner cannot publish mirrored
     * state before the releasing thread finishes its own state transition. */
    int error=pthread_mutex_unlock(&e->native);
    if(error)fatal("pthread_mutex_unlock",error);
    e->users--;
    pthread_mutex_unlock(&registry_mutex);
}
bool nso_host_is_mutex_locked_by_current_thread(const void *object) {
    struct thread_identity *identity=current_identity();
    struct mutex_entry *e=borrow((void*)object);
    pthread_mutex_lock(&registry_mutex);
    bool owned=e->depth && e->owner_handle==identity->handle;
    e->users--;
    pthread_mutex_unlock(&registry_mutex);
    return owned;
}
