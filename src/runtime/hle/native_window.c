/* Native window holder for the guest display bridge.
 *
 * Deliberately free of any Android header: the stored pointer is opaque here
 * so this file compiles unchanged for the macOS host and for Android arm64.
 * Only the Android JNI layer may interpret the pointer as an ANativeWindow. */
#include "native_window.h"

#include <stdatomic.h>
#include <pthread.h>

#include <time.h>

static _Atomic(void *) g_native_window;
static _Atomic(uint64_t) g_native_window_generation;
static pthread_mutex_t g_native_window_use_lock = PTHREAD_MUTEX_INITIALIZER;

void *nso_host_native_window_get(void) {
    return atomic_load_explicit(&g_native_window, memory_order_acquire);
}

void nso_host_native_window_set(void *window) {
    pthread_mutex_lock(&g_native_window_use_lock);
    atomic_store_explicit(&g_native_window, window, memory_order_release);
    atomic_fetch_add_explicit(&g_native_window_generation, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_native_window_use_lock);
}

void nso_host_native_window_clear(void) {
    pthread_mutex_lock(&g_native_window_use_lock);
    atomic_store_explicit(&g_native_window, NULL, memory_order_release);
    atomic_fetch_add_explicit(&g_native_window_generation, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_native_window_use_lock);
}

bool nso_host_native_window_use(void (*fn)(void *, void *), void *context) {
    if (!fn) return false;
    pthread_mutex_lock(&g_native_window_use_lock);
    void *window = atomic_load_explicit(&g_native_window, memory_order_acquire);
    if (window) fn(window, context);
    pthread_mutex_unlock(&g_native_window_use_lock);
    return window != NULL;
}

uint64_t nso_host_native_window_generation(void) {
    return atomic_load_explicit(&g_native_window_generation, memory_order_relaxed);
}

void *nso_host_native_window_wait_stable(int min_stable_ms, int timeout_ms) {
    if (min_stable_ms < 0) min_stable_ms = 0;
    if (timeout_ms < 0) timeout_ms = 0;
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        void *window = nso_host_native_window_get();
        if (window) {
            uint64_t generation = nso_host_native_window_generation();
            int settled_ms = 0;
            while (settled_ms < min_stable_ms) {
                struct timespec nap = {0, 10 * 1000 * 1000};
                nanosleep(&nap, NULL);
                settled_ms += 10;
                if (nso_host_native_window_get() != window ||
                    nso_host_native_window_generation() != generation) {
                    window = NULL;
                    break;
                }
            }
            if (window) return window;
        }
        struct timespec nap = {0, 10 * 1000 * 1000};
        nanosleep(&nap, NULL);
        waited_ms += 10;
    }
    return NULL;
}
