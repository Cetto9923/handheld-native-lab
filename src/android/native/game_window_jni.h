#ifndef FFRS_GAME_WINDOW_JNI_H
#define FFRS_GAME_WINDOW_JNI_H
#include <jni.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The window facts the game window form publishes for the runtime.
 *
 * pointer is the platform window (an ANativeWindow* on Android). width,
 * height and format are the buffer geometry of that window and are only
 * meaningful while present is non-zero. */
typedef struct ffrs_game_window_state {
    int present;
    uint64_t pointer;
    int32_t width;
    int32_t height;
    int32_t format;
    uint64_t generation;
} ffrs_game_window_state;

/* Publishes the platform window that belongs to a Java Surface.
 *
 * The Surface object is the only supported source: the platform window is
 * obtained with ANativeWindow_fromSurface, which takes a reference that this
 * layer releases again. A null Surface clears the published window instead of
 * inventing a placeholder. */
JNIEXPORT jlongArray JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_setSurface(JNIEnv* env, jclass type, jobject surface);

/* Releases the published platform window reference and clears the holder. */
JNIEXPORT void JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_clearSurface(JNIEnv* env, jclass type);

/* Reads the published window without touching a Java object. */
JNIEXPORT jlongArray JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_describeWindow(JNIEnv* env, jclass type);
#ifdef __cplusplus
}
#endif
#endif
