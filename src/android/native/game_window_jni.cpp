#include "game_window_jni.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <pthread.h>

#include "native_window.h"
#include "nso_host.h"
#include "hid_bridge.h"
#include "audio_bridge.h"

namespace {

constexpr const char* kLogTag = "FFRS-GameWindow";

/* Every holder access is serialized: the SurfaceHolder callback runs on the UI
 * thread while a runtime thread may read the published window. */
pthread_mutex_t g_window_lock = PTHREAD_MUTEX_INITIALIZER;

ffrs_game_window_state Measure(void* window) {
    ffrs_game_window_state state;
    state.present = window != nullptr;
    state.pointer = reinterpret_cast<uint64_t>(window);
    state.width = 0;
    state.height = 0;
    state.format = 0;
    state.generation = nso_host_native_window_generation();
    if (!window) return state;
    ANativeWindow* native_window = static_cast<ANativeWindow*>(window);
    state.width = ANativeWindow_getWidth(native_window);
    state.height = ANativeWindow_getHeight(native_window);
    state.format = ANativeWindow_getFormat(native_window);
    return state;
}

void LogState(const char* event, const ffrs_game_window_state& state, uint64_t released) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "window_state event=%s present=%d pointer=0x%llx width=%d height=%d format=%d "
        "generation=%llu released=0x%llx",
        event, state.present, static_cast<unsigned long long>(state.pointer), state.width,
        state.height, state.format, static_cast<unsigned long long>(state.generation),
        static_cast<unsigned long long>(released));
}

jlongArray ToJava(JNIEnv* env, const ffrs_game_window_state& state) {
    jlong values[6];
    values[0] = state.present;
    values[1] = static_cast<jlong>(state.pointer);
    values[2] = state.width;
    values[3] = state.height;
    values[4] = state.format;
    values[5] = static_cast<jlong>(state.generation);
    jlongArray array = env->NewLongArray(6);
    if (!array) return nullptr;
    env->SetLongArrayRegion(array, 0, 6, values);
    return array;
}

/* Drops the published window and releases the platform reference it holds. */
void ReleasePublishedWindow() {
    pthread_mutex_lock(&g_window_lock);
    void* window = nso_host_native_window_get();
    nso_host_native_window_clear();
    pthread_mutex_unlock(&g_window_lock);
    if (window) ANativeWindow_release(static_cast<ANativeWindow*>(window));
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_setFocusState(JNIEnv*, jclass, jint state) {
    nso_host_oe_set_host_focus(static_cast<uint32_t>(state));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_hostExitReady(JNIEnv*, jclass, jboolean request) {
    return nso_host_oe_host_exit_ready(request)?JNI_TRUE:JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_audioDevicesChanged(JNIEnv*, jclass) {
    ffrs_audio_devices_changed();
}

extern "C" JNIEXPORT void JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_setPadState(JNIEnv*, jclass, jboolean connected,
        jlong buttons, jint lx, jint ly, jint rx, jint ry) {
    ffrs_hid_publish(connected, static_cast<uint64_t>(buttons), lx, ly, rx, ry);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_setSurface(JNIEnv* env, jclass, jobject surface) {
    if (surface == nullptr) {
        // A missing Surface is not a window. Clear rather than publish a
        // placeholder the runtime could mistake for a real surface.
        ReleasePublishedWindow();
        ffrs_game_window_state state = Measure(nullptr);
        LogState("set_surface_null", state, 0);
        return ToJava(env, state);
    }
    // The supported way to reach the platform window of a Java Surface.
    // It takes a reference on the window that this layer owns and releases.
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
            "ANativeWindow_fromSurface returned NULL for the given Surface");
        ReleasePublishedWindow();
        ffrs_game_window_state state = Measure(nullptr);
        LogState("set_surface_failed", state, 0);
        return ToJava(env, state);
    }
    pthread_mutex_lock(&g_window_lock);
    void* previous = nso_host_native_window_get();
    nso_host_native_window_set(window);
    ffrs_game_window_state state = Measure(window);
    pthread_mutex_unlock(&g_window_lock);
    // The new reference is published; the replaced one is no longer needed.
    uint64_t released = 0;
    if (previous) {
        released = reinterpret_cast<uint64_t>(previous);
        ANativeWindow_release(static_cast<ANativeWindow*>(previous));
    }
    LogState("set_surface", state, released);
    return ToJava(env, state);
}

extern "C" JNIEXPORT void JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_clearSurface(JNIEnv*, jclass) {
    uint64_t released = 0;
    pthread_mutex_lock(&g_window_lock);
    void* window = nso_host_native_window_get();
    if (window) released = reinterpret_cast<uint64_t>(window);
    nso_host_native_window_clear();
    ffrs_game_window_state state = Measure(nullptr);
    pthread_mutex_unlock(&g_window_lock);
    if (window) ANativeWindow_release(static_cast<ANativeWindow*>(window));
    LogState("clear_surface", state, released);
}

extern "C" JNIEXPORT jlongArray JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_describeWindow(JNIEnv* env, jclass) {
    pthread_mutex_lock(&g_window_lock);
    ffrs_game_window_state state = Measure(nso_host_native_window_get());
    pthread_mutex_unlock(&g_window_lock);
    return ToJava(env, state);
}
