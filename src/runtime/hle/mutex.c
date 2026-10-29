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

#define OBJECT_HASH_SIZE 2048
#define OBJECT_HASH_MASK (OBJECT_HASH_SIZE - 1)
static inline size_t object_hash(const void *obj) {
    uintptr_t k = (uintptr_t)obj;
    k = (~k) + (k << 18);
    k = k ^ (k >> 31);
    k = k * 21;
    k = k ^ (k >> 11);
    k = k + (k << 6);
    k = k ^ (k >> 22);
    return (size_t)(k & OBJECT_HASH_MASK);
}

static pthread_mutex_t registry_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct mutex_entry *mutex_buckets[OBJECT_HASH_SIZE];
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
    size_t b = object_hash(object);
    struct mutex_entry *e = mutex_buckets[b];
    while(e && e->object != object) e = e->next;
    return e;
}
static struct mutex_entry *borrow(void *object) {
    if(!object || ((uintptr_t)object & 7)) fatal("unaligned MutexType", EINVAL);
    pthread_mutex_lock(&registry_mutex);
    struct mutex_entry *e=find(object);
    if(!e) {
        unsigned char *b = (unsigned char *)object;
        if(b[0] == 1) {
            /* Statically-initialized MutexType (NN_OS_MUTEX_INITIALIZER) */
            bool recursive = b[1] != 0;
            e = calloc(1, sizeof(*e));
            if(!e) fatal("mutex allocation", ENOMEM);
            pthread_mutexattr_t attr;
            int error = pthread_mutexattr_init(&attr);
            if(error) fatal("pthread_mutexattr_init", error);
            error = pthread_mutexattr_settype(&attr, recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL);
            if(error) fatal("pthread_mutexattr_settype", error);
            error = pthread_mutex_init(&e->native, &attr);
            pthread_mutexattr_destroy(&attr);
            if(error) fatal("pthread_mutex_init", error);
            e->object = object;
            size_t idx = object_hash(object);
            e->next = mutex_buckets[idx];
            mutex_buckets[idx] = e;
        } else {
            fatal("unregistered MutexType", EINVAL);
        }
    }
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
    size_t idx = object_hash(object);
    e->next = mutex_buckets[idx];
    mutex_buckets[idx] = e;
    unsigned char *bytes=object;
    bytes[1]=recursive;
    memcpy(bytes+4,&level,4);
    mirror(e);
    bytes[0]=1;
    pthread_mutex_unlock(&registry_mutex);
}
void nso_host_finalize_mutex(void *object) {
    if(!object || ((uintptr_t)object & 7)) fatal("unaligned MutexType", EINVAL);
    pthread_mutex_lock(&registry_mutex);
    size_t idx = object_hash(object);
    struct mutex_entry **slot = &mutex_buckets[idx];
    while(*slot && (*slot)->object!=object) slot = &(*slot)->next;
    if(!*slot) {
        unsigned char *b = (unsigned char *)object;
        if(b && b[0] == 1) {
            b[0] = 0;
            pthread_mutex_unlock(&registry_mutex);
            return;
        }
        fatal("FinalizeMutex while active/unregistered",EBUSY);
    }
    if((*slot)->users || (*slot)->depth)fatal("FinalizeMutex while active/unregistered",EBUSY);
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

struct condition_entry {
    void *object;
    pthread_cond_t native;
    unsigned users;
    struct condition_entry *next;
};
static struct condition_entry *conditions[OBJECT_HASH_SIZE];
static struct condition_entry *condition_borrow(void *object, int create) {
    if (!object || ((uintptr_t)object & 3)) fatal("unaligned ConditionVariableType", EINVAL);
    pthread_mutex_lock(&registry_mutex);
    size_t bucket = object_hash(object);
    struct condition_entry *entry = conditions[bucket];
    while (entry && entry->object != object) entry = entry->next;
    if (!entry && (create || *(unsigned char *)object == 1)) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) fatal("condition allocation", ENOMEM);
        pthread_condattr_t attr;
        int error = pthread_condattr_init(&attr);
        if (error) fatal("condition attributes", error);
        if (!error) error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!error) error = pthread_cond_init(&entry->native, &attr);
        pthread_condattr_destroy(&attr);
        if (error) fatal("condition init", error);
        entry->object = object;
        entry->next = conditions[bucket]; conditions[bucket] = entry;
    } else if (entry && create) fatal("duplicate InitializeConditionVariable", EINVAL);
    if (!entry) fatal("unregistered ConditionVariableType", EINVAL);
    entry->users++;
    pthread_mutex_unlock(&registry_mutex);
    return entry;
}
static void condition_drop(struct condition_entry *entry) {
    pthread_mutex_lock(&registry_mutex); entry->users--; pthread_mutex_unlock(&registry_mutex);
}
void nso_host_initialize_condition_variable(void *object) {
    struct condition_entry *entry = condition_borrow(object, 1);
    /* SDK20 initializer +0x13bbd0 writes initialized byte 0; the internal
     * condition at +4 is a zeroed 32-bit wait word. Padding is untouched. */
    *(uint32_t *)((unsigned char *)object + 4) = 0;
    *(unsigned char *)object = 1;
    condition_drop(entry);
}
void nso_host_finalize_condition_variable(void *object) {
    if (!object) fatal("FinalizeConditionVariable null", EINVAL);
    pthread_mutex_lock(&registry_mutex);
    struct condition_entry **slot = &conditions[object_hash(object)];
    while (*slot && (*slot)->object != object) slot = &(*slot)->next;
    if (!*slot && *(unsigned char *)object == 1) {
        *(unsigned char *)object = 0; pthread_mutex_unlock(&registry_mutex); return;
    }
    if (!*slot || (*slot)->users) fatal("FinalizeConditionVariable active/unregistered", EBUSY);
    struct condition_entry *entry = *slot;
    int error = pthread_cond_destroy(&entry->native);
    if (error) fatal("condition destroy", error);
    *slot = entry->next; *(unsigned char *)object = 0;
    pthread_mutex_unlock(&registry_mutex); free(entry);
}
static bool condition_wait(void *object, void *mutex, int timed, int64_t nanoseconds) {
    struct condition_entry *cv = condition_borrow(object, 0);
    struct mutex_entry *m = borrow(mutex);
    struct thread_identity *identity = current_identity();
    struct timespec deadline;
    if (timed) {
        if (clock_gettime(CLOCK_MONOTONIC, &deadline)) fatal("condition clock", errno);
        if (nanoseconds > 0) {
            deadline.tv_sec += nanoseconds / INT64_C(1000000000);
            deadline.tv_nsec += nanoseconds % INT64_C(1000000000);
            if (deadline.tv_nsec >= 1000000000) { deadline.tv_nsec -= 1000000000; deadline.tv_sec++; }
        }
    }
    pthread_mutex_lock(&registry_mutex);
    if (m->depth != 1 || m->owner_handle != identity->handle)
        fatal("condition wait requires single held mutex level", EPERM);
    m->depth = 0; m->owner_handle = 0; m->owner_token = 0; mirror(m);
    pthread_mutex_unlock(&registry_mutex);
    int error = timed ? pthread_cond_timedwait(&cv->native, &m->native, &deadline)
                      : pthread_cond_wait(&cv->native, &m->native);
    pthread_mutex_lock(&registry_mutex);
    if (m->depth || m->owner_handle) fatal("condition reacquire ownership", EINVAL);
    m->depth = 1; m->owner_handle = identity->handle; m->owner_token = (uintptr_t)identity->token;
    mirror(m); m->users--;
    pthread_mutex_unlock(&registry_mutex);
    condition_drop(cv);
    if (error && error != ETIMEDOUT) fatal("condition wait", error);
    /* SDK +0x149384 returns 0 on timeout; +0x14939c returns 1 on wake. */
    return error == 0;
}
void nso_host_wait_condition_variable(void *condition, void *mutex) {
    (void)condition_wait(condition, mutex, 0, 0);
}
bool nso_host_timed_wait_condition_variable(void *condition, void *mutex, int64_t nanoseconds) {
    return condition_wait(condition, mutex, 1, nanoseconds);
}
void nso_host_signal_condition_variable(void *object) {
    struct condition_entry *cv = condition_borrow(object, 0);
    int error = pthread_cond_signal(&cv->native); condition_drop(cv);
    if (error) fatal("condition signal", error);
}

struct rwlock_entry {
    void *object;
    pthread_rwlock_t native;
    uint32_t write_owner_handle;
    uintptr_t write_owner_token;
    uint32_t write_depth;
    uint32_t read_count;
    uint32_t users;
    struct rwlock_entry *next;
};
static pthread_mutex_t rwlock_registry_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct rwlock_entry *rwlock_buckets[OBJECT_HASH_SIZE];

static struct rwlock_entry *find_rwlock(void *object) {
    size_t b = object_hash(object);
    struct rwlock_entry *e = rwlock_buckets[b];
    while(e && e->object != object) e = e->next;
    return e;
}

static struct rwlock_entry *borrow_rwlock(void *object) {
    if(!object || ((uintptr_t)object & 7)) fatal("unaligned ReaderWriterLockType", EINVAL);
    pthread_mutex_lock(&rwlock_registry_mutex);
    struct rwlock_entry *e = find_rwlock(object);
    if(!e) {
        unsigned char *bytes = (unsigned char *)object;
        if(bytes[0x10] == 1) {
            e = calloc(1, sizeof(*e));
            if(!e) fatal("rwlock allocation", ENOMEM);
            int error = pthread_rwlock_init(&e->native, NULL);
            if(error) fatal("pthread_rwlock_init", error);
            e->object = object;
            size_t idx = object_hash(object);
            e->next = rwlock_buckets[idx];
            rwlock_buckets[idx] = e;
        } else {
            fatal("unregistered ReaderWriterLockType", EINVAL);
        }
    }
    e->users++;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    return e;
}

static void mirror_rwlock(struct rwlock_entry *e) {
    __atomic_store_n((uintptr_t*)((unsigned char*)e->object + 0x18), e->write_owner_token, __ATOMIC_RELEASE);
    __atomic_store_n((uint32_t*)((unsigned char*)e->object + 0x0c), e->write_depth, __ATOMIC_RELAXED);
    __atomic_store_n((uint32_t*)((unsigned char*)e->object + 0x04), e->read_count, __ATOMIC_RELAXED);
}

void nso_host_initialize_rwlock(void *object) {
    if(!object || ((uintptr_t)object & 7)) fatal("unaligned ReaderWriterLockType", EINVAL);
    struct rwlock_entry *e = calloc(1, sizeof(*e));
    if(!e) fatal("rwlock allocation", ENOMEM);
    int error = pthread_rwlock_init(&e->native, NULL);
    if(error) fatal("pthread_rwlock_init", error);
    e->object = object;
    pthread_mutex_lock(&rwlock_registry_mutex);
    if(find_rwlock(object)) fatal("duplicate InitializeReaderWriterLock", EINVAL);
    size_t idx = object_hash(object);
    e->next = rwlock_buckets[idx];
    rwlock_buckets[idx] = e;
    unsigned char *bytes = object;
    bytes[0x10] = 1;
    *(uintptr_t *)(bytes + 0x18) = 0;
    *(uint32_t *)(bytes + 0x0c) = 0;
    *(uint32_t *)(bytes + 0x20) = 0;
    pthread_mutex_unlock(&rwlock_registry_mutex);
}

void nso_host_finalize_rwlock(void *object) {
    if(!object || ((uintptr_t)object & 7)) fatal("unaligned ReaderWriterLockType", EINVAL);
    pthread_mutex_lock(&rwlock_registry_mutex);
    size_t idx = object_hash(object);
    struct rwlock_entry **slot = &rwlock_buckets[idx];
    while(*slot && (*slot)->object != object) slot = &(*slot)->next;
    if(!*slot) {
        unsigned char *b = (unsigned char *)object;
        if(b && b[0x10] == 1) {
            b[0x10] = 0;
            pthread_mutex_unlock(&rwlock_registry_mutex);
            return;
        }
        fatal("FinalizeReaderWriterLock while active/unregistered", EBUSY);
    }
    if((*slot)->users || (*slot)->write_depth || (*slot)->read_count)
        fatal("FinalizeReaderWriterLock while active/unregistered", EBUSY);
    struct rwlock_entry *e = *slot;
    int error = pthread_rwlock_destroy(&e->native);
    if(error) fatal("pthread_rwlock_destroy", error);
    ((unsigned char *)object)[0x10] = 0;
    *slot = e->next;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    free(e);
}

void nso_host_acquire_read_lock(void *object) {
    struct rwlock_entry *e = borrow_rwlock(object);
    int error = pthread_rwlock_rdlock(&e->native);
    if(error) fatal("pthread_rwlock_rdlock", error);
    pthread_mutex_lock(&rwlock_registry_mutex);
    e->read_count++;
    mirror_rwlock(e);
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
}

bool nso_host_try_acquire_read_lock(void *object) {
    struct rwlock_entry *e = borrow_rwlock(object);
    int error = pthread_rwlock_tryrdlock(&e->native);
    pthread_mutex_lock(&rwlock_registry_mutex);
    if(!error) {
        e->read_count++;
        mirror_rwlock(e);
    } else if(error != EBUSY) {
        fatal("pthread_rwlock_tryrdlock", error);
    }
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    return error == 0;
}

void nso_host_release_read_lock(void *object) {
    struct rwlock_entry *e = borrow_rwlock(object);
    pthread_mutex_lock(&rwlock_registry_mutex);
    if(!e->read_count) fatal("ReleaseReadLock without readers", EPERM);
    e->read_count--;
    mirror_rwlock(e);
    pthread_mutex_unlock(&rwlock_registry_mutex);
    int error = pthread_rwlock_unlock(&e->native);
    if(error) fatal("pthread_rwlock_unlock (read)", error);
    pthread_mutex_lock(&rwlock_registry_mutex);
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
}

void nso_host_acquire_write_lock(void *object) {
    struct thread_identity *identity = current_identity();
    struct rwlock_entry *e = borrow_rwlock(object);
    int error = pthread_rwlock_wrlock(&e->native);
    if(error) fatal("pthread_rwlock_wrlock", error);
    pthread_mutex_lock(&rwlock_registry_mutex);
    e->write_owner_handle = identity->handle;
    e->write_owner_token = (uintptr_t)identity->token;
    e->write_depth = 1;
    mirror_rwlock(e);
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
}

bool nso_host_try_acquire_write_lock(void *object) {
    struct thread_identity *identity = current_identity();
    struct rwlock_entry *e = borrow_rwlock(object);
    int error = pthread_rwlock_trywrlock(&e->native);
    pthread_mutex_lock(&rwlock_registry_mutex);
    if(!error) {
        e->write_owner_handle = identity->handle;
        e->write_owner_token = (uintptr_t)identity->token;
        e->write_depth = 1;
        mirror_rwlock(e);
    } else if(error != EBUSY) {
        fatal("pthread_rwlock_trywrlock", error);
    }
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    return error == 0;
}

void nso_host_release_write_lock(void *object) {
    struct thread_identity *identity = current_identity();
    struct rwlock_entry *e = borrow_rwlock(object);
    pthread_mutex_lock(&rwlock_registry_mutex);
    if(!e->write_depth || e->write_owner_handle != identity->handle)
        fatal("ReleaseWriteLock without ownership", EPERM);
    e->write_depth = 0;
    e->write_owner_handle = 0;
    e->write_owner_token = 0;
    mirror_rwlock(e);
    int error = pthread_rwlock_unlock(&e->native);
    if(error) fatal("pthread_rwlock_unlock (write)", error);
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
}

bool nso_host_is_write_lock_held_by_current_thread(const void *object) {
    struct thread_identity *identity = current_identity();
    struct rwlock_entry *e = borrow_rwlock((void *)object);
    pthread_mutex_lock(&rwlock_registry_mutex);
    bool owned = e->write_depth && e->write_owner_handle == identity->handle;
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    return owned;
}

bool nso_host_is_read_lock_held(const void *object) {
    struct rwlock_entry *e = borrow_rwlock((void *)object);
    pthread_mutex_lock(&rwlock_registry_mutex);
    bool held = e->read_count > 0;
    e->users--;
    pthread_mutex_unlock(&rwlock_registry_mutex);
    return held;
}

bool nso_host_is_rwlock_owner_thread(const void *object) {
    return nso_host_is_write_lock_held_by_current_thread(object);
}
