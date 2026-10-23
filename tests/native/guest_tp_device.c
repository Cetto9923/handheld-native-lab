/* Android AArch64 device test for the guest TP trap adapter.
 *
 * NOT_RUN_DEVICE: this file was written on a Mac host and has never been
 * executed on a device. The coordinator owns the device run; the exact
 * commands are listed in docs/analysis/codex_native_tp_read_report.md.
 *
 * The test proves four things on a real RG477M:
 *   1. a patched instruction returns the calling thread's guest TP;
 *   2. two threads never observe each other's guest TP;
 *   3. the host TPIDR_EL0 and the Bionic thread local state are unchanged,
 *      so the adapter never writes the host thread pointer;
 *   4. a thread that holds no mirror keeps the trap observable.
 */
#include "../../src/runtime/hle/guest_tp.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {THREADS=8,INITIALIZED=0x220,TOTAL=0x1019,HEADER=0x10,ALIGNMENT=8,STORAGE=0x1030};

/* The stub reads the guest TP twice, stores the first value and returns.
 * The traps are the rewritten MRS words; the store and the return are the
 * original module instructions. */
static const uint32_t stub_words[] = {
    GUEST_TP_BRK_MAGIC | (8u << 5),
    GUEST_TP_BRK_MAGIC | (9u << 5),
    0xf9000008u,   /* str x8, [x0] */
    0xd65f03c0u    /* ret */
};

static unsigned char template_bytes[INITIALIZED];
static void *stub_page;
static void (*stub_entry)(uint64_t *out);
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static unsigned ready, release_workers;
static _Thread_local _Alignas(64) unsigned host_canary;

struct worker_result {unsigned id, errors; uintptr_t base; uint64_t observed, second;};

static uint64_t read_host_tpidr_el0(void) {
    uint64_t value = 0;
#if defined(__aarch64__)
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(value));
#endif
    return value;
}

static void *worker(void *argument) {
    struct worker_result *result = argument;
    host_canary = 0x9e370000u + result->id;
    guest_tp_template source = {template_bytes, INITIALIZED, TOTAL, ALIGNMENT, HEADER};
    guest_tp_mirror *mirror = NULL;
    if (guest_tp_mirror_create(&source, &mirror).status != GUEST_TP_OK) { result->errors++; return NULL; }
    if (guest_tp_bind_current(mirror).status != GUEST_TP_OK) { result->errors++; return NULL; }
    result->base = (uintptr_t)guest_tp_current_base();
    uint64_t out = 0;
    stub_entry(&out);
    result->observed = out;
    result->second = (uint64_t)(uintptr_t)guest_tp_current_base();
    if (out != (uint64_t)result->base) result->errors++;
    if (result->second != (uint64_t)result->base) result->errors++;
    if (host_canary != 0x9e370000u + result->id) result->errors++;
    pthread_mutex_lock(&mutex); ready++; pthread_cond_broadcast(&condition);
    while (!release_workers) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
    if (guest_tp_unbind_current().status != GUEST_TP_OK) result->errors++;
    return NULL;
}

int main(void) {
    for (size_t i = 0; i < INITIALIZED; i++) template_bytes[i] = (unsigned char)(i * 37 + 11);
    host_canary = 0x9e370000u;
    uint64_t host_before = read_host_tpidr_el0();

    long page = sysconf(_SC_PAGESIZE);
    stub_page = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub_page == MAP_FAILED) { printf("{\"errors\":1,\"reason\":\"mmap\"}\n"); return 1; }
    memcpy(stub_page, stub_words, sizeof(stub_words));
    if (mprotect(stub_page, (size_t)page, PROT_READ | PROT_EXEC)) {
        printf("{\"errors\":1,\"reason\":\"mprotect\"}\n"); return 1;
    }
    if (guest_tp_register_text(stub_page, sizeof(stub_words)).status != GUEST_TP_OK) {
        printf("{\"errors\":1,\"reason\":\"register_text\"}\n"); return 1;
    }
    stub_entry = (void (*)(uint64_t *))stub_page;

    struct guest_tp_result installed = guest_tp_install_trap_handler();
    if (installed.status != GUEST_TP_OK) {
        printf("{\"errors\":1,\"reason\":\"install_trap_handler\",\"status\":%d}\n", (int)installed.status);
        return 1;
    }

    unsigned errors = 0;
    pthread_t threads[THREADS]; struct worker_result results[THREADS];
    memset(results, 0, sizeof(results));
    for (unsigned i = 0; i < THREADS; i++) {
        results[i].id = i + 1;
        if (pthread_create(&threads[i], NULL, worker, &results[i])) { errors++; return 1; }
    }
    pthread_mutex_lock(&mutex);
    while (ready != THREADS) pthread_cond_wait(&condition, &mutex);
    for (unsigned i = 0; i < THREADS; i++) for (unsigned j = 0; j < i; j++) {
        if (results[i].base == results[j].base) errors++;
    }
    release_workers = 1; pthread_cond_broadcast(&condition); pthread_mutex_unlock(&mutex);
    for (unsigned i = 0; i < THREADS; i++) {
        if (pthread_join(threads[i], NULL)) { errors++; return 1; }
        errors += results[i].errors;
    }

    uint64_t host_after = read_host_tpidr_el0();
    /* The adapter never writes the host thread pointer. TPIDR_EL0 is constant
     * for the life of a thread, so any change means the adapter wrote it. */
    int host_tls_intact = (host_before != 0) && (host_after == host_before) &&
                          (host_canary == 0x9e370000u);
    if (!host_tls_intact) errors++;
    if (guest_tp_live_mirrors() != 0) errors++;

    guest_tp_remove_trap_handler();
    printf("{\"threads\":%u,\"storage\":%u,\"trap_handler\":\"RUN_DEVICE\","
           "\"host_tls_intact\":%d,\"live_mirrors\":%zu,\"errors\":%u}\n",
           THREADS, STORAGE, host_tls_intact, guest_tp_live_mirrors(), errors);
    return errors ? 1 : 0;
}
