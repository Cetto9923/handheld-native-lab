#include "guest_os_tls.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct tls_slot { int allocated; uint64_t destructor; };
struct guest_os_tls_thread {
    uint64_t values[GUEST_OS_TLS_SLOT_COUNT];
    enum guest_os_tls_thread_state state;
    struct guest_os_tls_cleanup_report report;
    struct guest_os_tls_thread *next;
};
struct guest_os_tls_manager {
    pthread_mutex_t mutex;
    struct tls_slot slots[GUEST_OS_TLS_SLOT_COUNT];
    struct guest_os_tls_thread *threads, *cleaning;
    int quarantined;
};
static struct guest_os_tls_result result(enum guest_os_tls_status status) {
    return (struct guest_os_tls_result){status,0};
}
static struct guest_os_tls_result lock(guest_os_tls_manager *m) {
    if (!m) return result(GUEST_OS_TLS_INVALID);
    int e = pthread_mutex_lock(&m->mutex);
    return (struct guest_os_tls_result){e ? GUEST_OS_TLS_SYSTEM_ERROR : GUEST_OS_TLS_OK,e};
}
static enum guest_os_tls_status slot_status(uint32_t slot) {
    if (slot >= GUEST_OS_TLS_SLOT_COUNT) return GUEST_OS_TLS_INVALID;
    return slot == GUEST_OS_TLS_RESERVED_SLOT ? GUEST_OS_TLS_RESERVED : GUEST_OS_TLS_OK;
}
/* Compare opaque handle identities before dereferencing an input handle. */
static struct guest_os_tls_thread **thread_link(guest_os_tls_manager *m, guest_os_tls_thread *t) {
    struct guest_os_tls_thread **p = &m->threads;
    while (*p && *p != t) p = &(*p)->next;
    return p;
}
static void report(guest_os_tls_thread *t, struct guest_os_tls_cleanup_report *out) {
    *out = t->report;
    out->state = t->state;
    out->remaining_nonzero = 0;
    for (unsigned i=0;i<GUEST_OS_TLS_SLOT_COUNT;++i) out->remaining_nonzero += t->values[i] != 0;
}
struct guest_os_tls_result guest_os_tls_create(guest_os_tls_manager **out) {
    if (!out) return result(GUEST_OS_TLS_INVALID);
    *out = NULL;
    guest_os_tls_manager *m = calloc(1,sizeof(*m));
    if (!m) return result(GUEST_OS_TLS_NO_MEMORY);
    int e = pthread_mutex_init(&m->mutex,NULL);
    if (e) { free(m); return (struct guest_os_tls_result){GUEST_OS_TLS_SYSTEM_ERROR,e}; }
    *out = m;
    return result(GUEST_OS_TLS_OK);
}
struct guest_os_tls_result guest_os_tls_register(guest_os_tls_manager *m, guest_os_tls_thread **out) {
    if (!m || !out) return result(GUEST_OS_TLS_INVALID);
    *out = NULL;
    guest_os_tls_thread *t = calloc(1,sizeof(*t));
    if (!t) return result(GUEST_OS_TLS_NO_MEMORY);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) { free(t); return r; }
    if (m->quarantined) { r.status = GUEST_OS_TLS_QUARANTINED; free(t); }
    else { t->state = GUEST_OS_TLS_THREAD_LIVE; t->next = m->threads; m->threads = t; *out = t; }
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_allocate(guest_os_tls_manager *m, enum guest_os_tls_pool pool,
                                               uint64_t destructor, uint32_t *out) {
    if (!out || (pool != GUEST_OS_TLS_APP && pool != GUEST_OS_TLS_SDK)) return result(GUEST_OS_TLS_INVALID);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (m->quarantined) { r.status = GUEST_OS_TLS_QUARANTINED; goto done; }
    int slot = pool == GUEST_OS_TLS_APP ? 0 : 31;
    const int stop = 16;
    int step = pool == GUEST_OS_TLS_APP ? 1 : -1;
    while (slot != stop && m->slots[slot].allocated) slot += step;
    if (slot == stop) { r.status = GUEST_OS_TLS_EXHAUSTED; goto done; }
    /* The manager owns both tables under one lock. No allocation or fallible
     * action remains after publishing the occupied marker. All values clear
     * before the only write to the output (the SDK's verified publication order). */
    m->slots[slot] = (struct tls_slot){1,destructor};
    for (guest_os_tls_thread *t = m->threads; t; t = t->next) t->values[slot] = 0;
    *out = (uint32_t)slot;
done:
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_free(guest_os_tls_manager *m, uint32_t slot) {
    enum guest_os_tls_status s = slot_status(slot);
    if (s != GUEST_OS_TLS_OK) return result(s);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (m->quarantined) r.status = GUEST_OS_TLS_QUARANTINED;
    else if (!m->slots[slot].allocated) r.status = GUEST_OS_TLS_NOT_ALLOCATED;
    else m->slots[slot] = (struct tls_slot){0,0};
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_get(guest_os_tls_manager *m, guest_os_tls_thread *t,
                                          uint32_t slot, uint64_t *out) {
    if (!out) return result(GUEST_OS_TLS_INVALID);
    enum guest_os_tls_status s = slot_status(slot);
    if (s != GUEST_OS_TLS_OK) return result(s);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (!*thread_link(m,t)) r.status = GUEST_OS_TLS_NOT_REGISTERED;
    else *out = t->values[slot];
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_set(guest_os_tls_manager *m, guest_os_tls_thread *t,
                                          uint32_t slot, uint64_t value) {
    enum guest_os_tls_status s = slot_status(slot);
    if (s != GUEST_OS_TLS_OK) return result(s);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (!*thread_link(m,t)) r.status = GUEST_OS_TLS_NOT_REGISTERED;
    else if (m->quarantined) r.status = GUEST_OS_TLS_QUARANTINED;
    else if (t->state != GUEST_OS_TLS_THREAD_LIVE && t->state != GUEST_OS_TLS_THREAD_CLEANING) r.status = GUEST_OS_TLS_BAD_STATE;
    else t->values[slot] = value;
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_count(guest_os_tls_manager *m, struct guest_os_tls_counts *out) {
    if (!out) return result(GUEST_OS_TLS_INVALID);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    struct guest_os_tls_counts c = {0};
    for (unsigned i=0;i<16;++i) c.app_used += m->slots[i].allocated != 0;
    for (unsigned i=17;i<32;++i) c.sdk_used += m->slots[i].allocated != 0;
    for (guest_os_tls_thread *t = m->threads; t; t = t->next) {
        ++c.registered_threads;
        c.cleaning_threads += t->state == GUEST_OS_TLS_THREAD_CLEANING;
        c.cleaned_threads += t->state == GUEST_OS_TLS_THREAD_CLEANED;
        c.quarantined_threads += t->state == GUEST_OS_TLS_THREAD_QUARANTINED;
    }
    c.quarantined = m->quarantined; *out = c;
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_report(guest_os_tls_manager *m, guest_os_tls_thread *t,
                                             struct guest_os_tls_cleanup_report *out) {
    if (!out) return result(GUEST_OS_TLS_INVALID);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (!*thread_link(m,t)) r.status = GUEST_OS_TLS_NOT_REGISTERED;
    else report(t,out);
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_cleanup(guest_os_tls_manager *m, guest_os_tls_thread *t,
                                              guest_os_tls_invoker invoke, void *opaque,
                                              struct guest_os_tls_cleanup_report *out) {
    if (!out) return result(GUEST_OS_TLS_INVALID);
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (!*thread_link(m,t)) { r.status = GUEST_OS_TLS_NOT_REGISTERED; goto done; }
    if (m->quarantined) { r.status = GUEST_OS_TLS_QUARANTINED; goto copy; }
    if (m->cleaning) { r.status = GUEST_OS_TLS_BUSY; goto copy; }
    if (t->state != GUEST_OS_TLS_THREAD_LIVE) { r.status = GUEST_OS_TLS_BAD_STATE; goto copy; }
    if (!invoke) {
        for (unsigned i=0;i<32;++i)
            if (m->slots[i].allocated && m->slots[i].destructor) {
                r.status = GUEST_OS_TLS_NEEDS_INVOKER; goto copy;
            }
    }
    t->state = GUEST_OS_TLS_THREAD_CLEANING; m->cleaning = t;
    memset(&t->report,0,sizeof(t->report));
    for (unsigned round=1;round<=5;++round) {
        int any_nonzero = 0;
        t->report.rounds_scanned = round;
        for (uint32_t slot=0;slot<32;++slot) {
            uint64_t value = t->values[slot];
            if (round != 1 && !value) continue;
            any_nonzero |= value != 0;
            struct tls_slot callback = m->slots[slot];
            if (callback.allocated && callback.destructor) {
                ++t->report.invocations;
                /* Pinning is the CLEANING state, not an SDK/host thread object.
                 * Unregister/destroy cannot reclaim t/m across this call. */
                pthread_mutex_unlock(&m->mutex);
                struct guest_os_tls_invoke_result invoked = invoke(m,t,callback.destructor,value,opaque);
                pthread_mutex_lock(&m->mutex);
                if (invoked.status != GUEST_OS_TLS_INVOKE_OK) {
                    t->report.failed_round = round; t->report.failed_slot = slot;
                    t->report.failed_destructor = callback.destructor; t->report.failed_value = value;
                    t->report.invocation = invoked;
                    t->state = GUEST_OS_TLS_THREAD_QUARANTINED;
                    m->quarantined = 1; m->cleaning = NULL;
                    r.status = GUEST_OS_TLS_QUARANTINED;
                    goto copy;
                }
            } else if (callback.allocated) ++t->report.noop_visits;
            /* Clear after return, even if callback freed/reallocated this slot
             * or rewrote its current value. Earlier slots can survive scan 5. */
            t->values[slot] = 0;
        }
        if (round > 1 && !any_nonzero) break;
    }
    t->state = GUEST_OS_TLS_THREAD_CLEANED; m->cleaning = NULL;
copy:
    report(t,out);
done:
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_unregister(guest_os_tls_manager *m, guest_os_tls_thread *t) {
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    struct guest_os_tls_thread **link = thread_link(m,t);
    if (!*link) r.status = GUEST_OS_TLS_NOT_REGISTERED;
    else if (m->quarantined) r.status = GUEST_OS_TLS_QUARANTINED;
    else if (t->state == GUEST_OS_TLS_THREAD_CLEANING) r.status = GUEST_OS_TLS_BUSY;
    else if (t->state != GUEST_OS_TLS_THREAD_CLEANED) r.status = GUEST_OS_TLS_BAD_STATE;
    else { *link = t->next; free(t); }
    pthread_mutex_unlock(&m->mutex);
    return r;
}
struct guest_os_tls_result guest_os_tls_destroy(guest_os_tls_manager *m) {
    struct guest_os_tls_result r = lock(m);
    if (r.status != GUEST_OS_TLS_OK) return r;
    if (m->threads || m->cleaning) { pthread_mutex_unlock(&m->mutex); return result(GUEST_OS_TLS_BUSY); }
    pthread_mutex_unlock(&m->mutex);
    int e = pthread_mutex_destroy(&m->mutex);
    if (e) return (struct guest_os_tls_result){GUEST_OS_TLS_SYSTEM_ERROR,e};
    free(m);
    return r;
}
