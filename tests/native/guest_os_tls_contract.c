#include "../../src/runtime/hle/guest_os_tls.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_uint checks,errors;
static unsigned reentry_cases, recovery_cases, retained_managers, retained_threads;
#define CHECK(c) do { ++checks; if (!(c)) { ++errors; fprintf(stderr,"line %d: %s\n",__LINE__,#c); } } while (0)
#define OK(c) CHECK((c).status == GUEST_OS_TLS_OK)
static uint64_t value(guest_os_tls_manager *m,guest_os_tls_thread *t,uint32_t s) {
    uint64_t v = UINT64_MAX; OK(guest_os_tls_get(m,t,s,&v)); return v;
}
static void close_thread(guest_os_tls_manager *m,guest_os_tls_thread *t) {
    struct guest_os_tls_cleanup_report report;
    OK(guest_os_tls_cleanup(m,t,NULL,NULL,&report));
    CHECK(report.state == GUEST_OS_TLS_THREAD_CLEANED && !report.remaining_nonzero);
    OK(guest_os_tls_unregister(m,t));
}
static void ranges(void) {
    guest_os_tls_manager *m = NULL; guest_os_tls_thread *a = NULL,*b = NULL,*c = NULL;
    OK(guest_os_tls_create(&m)); OK(guest_os_tls_register(m,&a)); OK(guest_os_tls_register(m,&b));
    uint32_t app[16],sdk[15];
    for (uint32_t i=0;i<32;++i) if (i != 16) {
        OK(guest_os_tls_set(m,a,i,100+i)); OK(guest_os_tls_set(m,b,i,200+i));
    }
    for (unsigned i=0;i<16;++i) {
        OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,0,&app[i])); CHECK(app[i] == i);
        CHECK(!value(m,a,app[i]) && !value(m,b,app[i]));
    }
    for (unsigned i=0;i<15;++i) {
        OK(guest_os_tls_allocate(m,GUEST_OS_TLS_SDK,0,&sdk[i])); CHECK(sdk[i] == 31-i);
        CHECK(!value(m,a,sdk[i]) && !value(m,b,sdk[i]));
    }
    struct guest_os_tls_counts counts;
    OK(guest_os_tls_count(m,&counts));
    CHECK(counts.app_used == 16 && counts.sdk_used == 15 && counts.registered_threads == 2);
    uint32_t out = 0xabcdef01;
    CHECK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,42,&out).status == GUEST_OS_TLS_EXHAUSTED && out == 0xabcdef01);
    CHECK(guest_os_tls_allocate(m,GUEST_OS_TLS_SDK,42,&out).status == GUEST_OS_TLS_EXHAUSTED && out == 0xabcdef01);
    CHECK(guest_os_tls_allocate(m,(enum guest_os_tls_pool)2,0,&out).status == GUEST_OS_TLS_INVALID && out == 0xabcdef01);
    uint64_t kept = 99;
    CHECK(guest_os_tls_get(m,a,16,&kept).status == GUEST_OS_TLS_RESERVED && kept == 99);
    CHECK(guest_os_tls_get(m,a,32,&kept).status == GUEST_OS_TLS_INVALID && kept == 99);
    CHECK(guest_os_tls_set(m,a,16,1).status == GUEST_OS_TLS_RESERVED);
    CHECK(guest_os_tls_free(m,16).status == GUEST_OS_TLS_RESERVED);
    CHECK(guest_os_tls_free(m,32).status == GUEST_OS_TLS_INVALID);
    OK(guest_os_tls_set(m,a,3,11)); OK(guest_os_tls_set(m,b,3,22));
    OK(guest_os_tls_free(m,3)); CHECK(value(m,a,3) == 11 && value(m,b,3) == 22);
    CHECK(guest_os_tls_free(m,3).status == GUEST_OS_TLS_NOT_ALLOCATED);
    OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,0,&out)); CHECK(out == 3);
    CHECK(!value(m,a,3) && !value(m,b,3));
    OK(guest_os_tls_set(m,a,31,33)); OK(guest_os_tls_set(m,b,31,44));
    OK(guest_os_tls_free(m,31)); CHECK(value(m,a,31) == 33 && value(m,b,31) == 44);
    OK(guest_os_tls_allocate(m,GUEST_OS_TLS_SDK,0,&out)); CHECK(out == 31);
    CHECK(!value(m,a,31) && !value(m,b,31));
    OK(guest_os_tls_set(m,a,0,77));
    OK(guest_os_tls_register(m,&c)); CHECK(!value(m,c,0) && value(m,a,0) == 77);
    CHECK(guest_os_tls_get(m,NULL,0,&kept).status == GUEST_OS_TLS_NOT_REGISTERED);
    CHECK(guest_os_tls_unregister(m,a).status == GUEST_OS_TLS_BAD_STATE);
    CHECK(guest_os_tls_destroy(m).status == GUEST_OS_TLS_BUSY);
    struct guest_os_tls_cleanup_report report;
    OK(guest_os_tls_cleanup(m,a,NULL,NULL,&report));
    CHECK(report.noop_visits == 31 && !report.invocations && report.rounds_scanned == 2 && !report.remaining_nonzero);
    CHECK(guest_os_tls_set(m,a,0,1).status == GUEST_OS_TLS_BAD_STATE);
    CHECK(guest_os_tls_cleanup(m,a,NULL,NULL,&report).status == GUEST_OS_TLS_BAD_STATE);
    OK(guest_os_tls_unregister(m,a)); close_thread(m,b); close_thread(m,c);
    for (uint32_t i=0;i<32;++i) if (i != 16) OK(guest_os_tls_free(m,i));
    OK(guest_os_tls_count(m,&counts)); CHECK(!counts.app_used && !counts.sdk_used && !counts.registered_threads);
    OK(guest_os_tls_destroy(m));
}

struct ordering {
    unsigned a_calls,b_calls,c_calls,n;
    uint64_t ids[16],values[16];
    guest_os_tls_thread *second,*registered_in_callback;
};
static struct guest_os_tls_invoke_result ordered(guest_os_tls_manager *m,guest_os_tls_thread *t,
                                                uint64_t id,uint64_t v,void *opaque) {
    struct ordering *o = opaque;
    CHECK(o->n < 16);
    if (o->n < 16) { o->ids[o->n] = id; o->values[o->n++] = v; }
    if (id == 10) {
        ++o->a_calls;
        CHECK(value(m,t,0) == v);
        OK(guest_os_tls_set(m,t,0,999)); /* overwritten by post-return clear */
        OK(guest_os_tls_set(m,t,1,200+o->a_calls));
        if (o->a_calls == 1) {
            struct guest_os_tls_counts counts; OK(guest_os_tls_count(m,&counts));
            CHECK(counts.cleaning_threads == 1);
            struct guest_os_tls_cleanup_report nested;
            CHECK(guest_os_tls_cleanup(m,t,ordered,opaque,&nested).status == GUEST_OS_TLS_BUSY);
            CHECK(guest_os_tls_cleanup(m,o->second,ordered,opaque,&nested).status == GUEST_OS_TLS_BUSY);
            CHECK(guest_os_tls_unregister(m,t).status == GUEST_OS_TLS_BUSY);
            CHECK(guest_os_tls_destroy(m).status == GUEST_OS_TLS_BUSY);
            OK(guest_os_tls_set(m,t,2,55)); OK(guest_os_tls_set(m,o->second,2,66));
            OK(guest_os_tls_free(m,2));
            CHECK(value(m,t,2) == 55 && value(m,o->second,2) == 66);
            uint32_t slot = 99;
            OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,13,&slot)); CHECK(slot == 2);
            CHECK(!value(m,t,2) && !value(m,o->second,2));
            OK(guest_os_tls_allocate(m,GUEST_OS_TLS_SDK,0,&slot)); CHECK(slot == 31);
            OK(guest_os_tls_register(m,&o->registered_in_callback));
            CHECK(!value(m,o->registered_in_callback,0));
            ++reentry_cases;
        }
    } else if (id == 11) {
        ++o->b_calls;
        CHECK(v == 200+o->b_calls && !value(m,t,0));
        OK(guest_os_tls_set(m,t,0,300+o->b_calls));
    } else if (id == 13) { ++o->c_calls; CHECK(v == 0); }
    else CHECK(0); /* old callback 12 was freed before its scan position */
    return (struct guest_os_tls_invoke_result){GUEST_OS_TLS_INVOKE_OK,0};
}
static void ordering(void) {
    guest_os_tls_manager *m = NULL; guest_os_tls_thread *t = NULL;
    struct ordering o = {0};
    OK(guest_os_tls_create(&m)); OK(guest_os_tls_register(m,&t)); OK(guest_os_tls_register(m,&o.second));
    for (uint32_t i=0;i<4;++i) { uint32_t slot=99; OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,i<3?10+i:0,&slot)); CHECK(slot == i); }
    struct guest_os_tls_cleanup_report report;
    OK(guest_os_tls_cleanup(m,t,ordered,&o,&report));
    CHECK(report.rounds_scanned == 5 && report.invocations == 11 && report.noop_visits == 2);
    CHECK(report.state == GUEST_OS_TLS_THREAD_CLEANED && report.remaining_nonzero == 1);
    CHECK(o.a_calls == 5 && o.b_calls == 5 && o.c_calls == 1 && o.n == 11);
    for (unsigned i=0,pos=0;i<5;++i) {
        CHECK(o.ids[pos] == 10 && o.values[pos] == (i?300+i:0)); ++pos;
        CHECK(o.ids[pos] == 11 && o.values[pos] == 201+i); ++pos;
        if (!i) { CHECK(o.ids[pos] == 13 && !o.values[pos]); ++pos; }
    }
    CHECK(value(m,t,0) == 305 && !value(m,t,1) && !value(m,t,2));
    unsigned invoked = o.n;
    CHECK(guest_os_tls_cleanup(m,t,ordered,&o,&report).status == GUEST_OS_TLS_BAD_STATE && o.n == invoked);
    OK(guest_os_tls_unregister(m,t)); /* no host free on opaque value 305 */
    for (uint32_t i=0;i<4;++i) OK(guest_os_tls_free(m,i));
    close_thread(m,o.second); close_thread(m,o.registered_in_callback);
    OK(guest_os_tls_destroy(m));
}

static struct guest_os_tls_invoke_result count_callback(guest_os_tls_manager *m,guest_os_tls_thread *t,
                                                       uint64_t id,uint64_t v,void *opaque) {
    (void)m; (void)t; CHECK(id == 42 && v == 77); ++*(unsigned *)opaque;
    return (struct guest_os_tls_invoke_result){GUEST_OS_TLS_INVOKE_OK,0};
}
static void needs_invoker(void) {
    guest_os_tls_manager *m = NULL; guest_os_tls_thread *t = NULL; uint32_t slot=99; unsigned calls=0;
    OK(guest_os_tls_create(&m)); OK(guest_os_tls_register(m,&t));
    OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,42,&slot)); CHECK(slot == 0);
    OK(guest_os_tls_set(m,t,0,77)); OK(guest_os_tls_set(m,t,1,88));
    struct guest_os_tls_cleanup_report report;
    CHECK(guest_os_tls_cleanup(m,t,NULL,NULL,&report).status == GUEST_OS_TLS_NEEDS_INVOKER);
    CHECK(report.state == GUEST_OS_TLS_THREAD_LIVE && !report.invocations && !report.rounds_scanned && report.remaining_nonzero == 2);
    CHECK(value(m,t,0) == 77 && value(m,t,1) == 88);
    OK(guest_os_tls_cleanup(m,t,count_callback,&calls,&report));
    CHECK(calls == 1 && report.invocations == 1 && !report.remaining_nonzero);
    OK(guest_os_tls_unregister(m,t)); OK(guest_os_tls_destroy(m));
}

struct worker { guest_os_tls_manager *manager; unsigned index; };
static void *worker(void *opaque) {
    struct worker *w = opaque;
    for (unsigned i=0;i<16;++i) {
        guest_os_tls_thread *t = NULL; OK(guest_os_tls_register(w->manager,&t));
        uint64_t pattern = (uint64_t)(w->index+1)*1000+i;
        OK(guest_os_tls_set(w->manager,t,0,pattern)); CHECK(value(w->manager,t,0) == pattern);
        close_thread(w->manager,t);
    }
    return NULL;
}
static void concurrent(void) {
    guest_os_tls_manager *m = NULL; uint32_t slot = 99;
    OK(guest_os_tls_create(&m)); OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,0,&slot));
    pthread_t threads[8]; struct worker args[8];
    for (unsigned i=0;i<8;++i) { args[i] = (struct worker){m,i}; CHECK(!pthread_create(&threads[i],NULL,worker,&args[i])); }
    for (unsigned i=0;i<8;++i) CHECK(!pthread_join(threads[i],NULL));
    struct guest_os_tls_counts counts; OK(guest_os_tls_count(m,&counts));
    CHECK(!counts.registered_threads && counts.app_used == 1 && !counts.quarantined);
    OK(guest_os_tls_destroy(m));
}

#if defined(GUEST_OS_TLS_CONTRACT_RECOVERY)
struct failure { unsigned calls; enum guest_os_tls_invoke_status status; };
static struct guest_os_tls_invoke_result fail_callback(guest_os_tls_manager *m,guest_os_tls_thread *t,
                                                      uint64_t id,uint64_t v,void *opaque) {
    struct failure *f = opaque; ++f->calls; CHECK(id == 900 && v == 111);
    OK(guest_os_tls_set(m,t,0,333));
    OK(guest_os_tls_free(m,1)); CHECK(value(m,t,1) == 222);
    uint32_t slot=99; OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,902,&slot)); CHECK(slot == 1);
    OK(guest_os_tls_set(m,t,1,444));
    return (struct guest_os_tls_invoke_result){f->status,EIO};
}
static void failure(enum guest_os_tls_invoke_status status) {
    guest_os_tls_manager *m = NULL; guest_os_tls_thread *t = NULL;
    OK(guest_os_tls_create(&m)); OK(guest_os_tls_register(m,&t));
    uint32_t slot=99;
    OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,900,&slot)); CHECK(slot == 0);
    OK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,901,&slot)); CHECK(slot == 1);
    OK(guest_os_tls_set(m,t,0,111)); OK(guest_os_tls_set(m,t,1,222));
    struct failure f = {0,status}; struct guest_os_tls_cleanup_report report;
    CHECK(guest_os_tls_cleanup(m,t,fail_callback,&f,&report).status == GUEST_OS_TLS_QUARANTINED);
    CHECK(f.calls == 1 && report.invocations == 1 && report.failed_round == 1 && !report.failed_slot);
    CHECK(report.failed_destructor == 900 && report.failed_value == 111 && report.invocation.status == status && report.invocation.provider_error == EIO);
    CHECK(report.state == GUEST_OS_TLS_THREAD_QUARANTINED && report.remaining_nonzero == 2);
    CHECK(value(m,t,0) == 333 && value(m,t,1) == 444);
    CHECK(guest_os_tls_cleanup(m,t,fail_callback,&f,&report).status == GUEST_OS_TLS_QUARANTINED && f.calls == 1);
    CHECK(guest_os_tls_set(m,t,0,555).status == GUEST_OS_TLS_QUARANTINED);
    CHECK(guest_os_tls_free(m,0).status == GUEST_OS_TLS_QUARANTINED);
    slot = 0x12345678;
    CHECK(guest_os_tls_allocate(m,GUEST_OS_TLS_APP,0,&slot).status == GUEST_OS_TLS_QUARANTINED && slot == 0x12345678);
    guest_os_tls_thread *new_thread = NULL;
    CHECK(guest_os_tls_register(m,&new_thread).status == GUEST_OS_TLS_QUARANTINED && !new_thread);
    CHECK(guest_os_tls_unregister(m,t).status == GUEST_OS_TLS_QUARANTINED);
    CHECK(guest_os_tls_destroy(m).status == GUEST_OS_TLS_BUSY);
    OK(guest_os_tls_report(m,t,&report)); CHECK(report.failed_destructor == 900 && report.invocations == 1);
    struct guest_os_tls_counts counts; OK(guest_os_tls_count(m,&counts));
    CHECK(counts.app_used == 2 && counts.registered_threads == 1 && counts.quarantined_threads == 1 && counts.quarantined);
    /* Deliberate retained ownership until process teardown, not cleanup success.
     * There is no component API that discards this uncertain callback state. */
    ++retained_managers; ++retained_threads; ++recovery_cases;
}
#endif

int main(void) {
    ranges(); ordering(); needs_invoker(); concurrent();
#if defined(GUEST_OS_TLS_CONTRACT_RECOVERY)
    failure(GUEST_OS_TLS_INVOKE_FAILED); failure(GUEST_OS_TLS_INVOKE_UNKNOWN);
#endif
    unsigned e = atomic_load(&errors);
    printf("{\"checks\":%u,\"errors\":%u,\"app_slots\":16,\"sdk_slots\":15,\"reserved_slot\":16,\"round_limit\":5,\"reentry_cases\":%u,\"native_workers\":8,\"cycles_per_worker\":16,\"recovery_cases\":%u,\"retained_managers\":%u,\"retained_threads\":%u}\n",
        atomic_load(&checks),e,reentry_cases,recovery_cases,retained_managers,retained_threads);
    return e ? 1 : 0;
}
