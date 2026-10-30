#ifndef NSO_HOST_NATIVE_WINDOW_H
#define NSO_HOST_NATIVE_WINDOW_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The platform window the guest display bridge hands out.
 *
 * On Android the stored pointer is an ANativeWindow*. This translation unit
 * never dereferences it and never includes <android/native_window.h>, so it
 * stays buildable on a plain host without the NDK. The Android JNI layer owns
 * the ANativeWindow type conversion and the reference count.
 *
 * Scope: this holder only publishes a window. It is not wired into the
 * runtime import path yet; nn::vi::GetNativeWindow binding is owned by the
 * nso_host bridge and must read the window through nso_host_native_window_get.
 */
void *nso_host_native_window_get(void);
/* Publishes window. The caller keeps ownership of the platform reference. */
void nso_host_native_window_set(void *window);
/* Drops the published window without touching the platform reference. */
void nso_host_native_window_clear(void);
/* Counts set/clear transitions so a reader can tell a fresh window from a
 * stale pointer without dereferencing anything. */
uint64_t nso_host_native_window_generation(void);

/* Waits for a platform window that stays published for min_stable_ms and
 * returns it, or NULL when none stayed. The Activity replaces its Surface
 * when an overlay takes the foreground, so a window that exists for a few
 * milliseconds is not one the guest can present into. */
void *nso_host_native_window_wait_stable(int min_stable_ms, int timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
