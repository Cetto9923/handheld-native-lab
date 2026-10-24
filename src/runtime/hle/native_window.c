/* Native window holder for the guest display bridge.
 *
 * Deliberately free of any Android header: the stored pointer is opaque here
 * so this file compiles unchanged for the macOS host and for Android arm64.
 * Only the Android JNI layer may interpret the pointer as an ANativeWindow. */
#include "native_window.h"

#include <stdatomic.h>

static _Atomic(void *) g_native_window;
static _Atomic(uint64_t) g_native_window_generation;

void *nso_host_native_window_get(void) {
    return atomic_load_explicit(&g_native_window, memory_order_acquire);
}

void nso_host_native_window_set(void *window) {
    atomic_store_explicit(&g_native_window, window, memory_order_release);
    atomic_fetch_add_explicit(&g_native_window_generation, 1, memory_order_relaxed);
}

void nso_host_native_window_clear(void) {
    atomic_store_explicit(&g_native_window, NULL, memory_order_release);
    atomic_fetch_add_explicit(&g_native_window_generation, 1, memory_order_relaxed);
}

uint64_t nso_host_native_window_generation(void) {
    return atomic_load_explicit(&g_native_window_generation, memory_order_relaxed);
}
