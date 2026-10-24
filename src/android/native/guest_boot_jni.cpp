// Starts the owned guest inside the process that owns the game window.
//
// The window is a process-local platform handle: an ANativeWindow that the
// Activity published cannot be handed to a guest running in another process.
// The guest therefore runs here, on its own thread inside the app process,
// through the same boot code the standalone CLI uses (src/runtime/nso/
// nso_boot.c). Nothing in this file fabricates a frame: it starts the guest
// and reports what the guest actually did.

#include <android/log.h>
#include <jni.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <new>

extern "C" {
#include "nso_boot.h"
}

namespace {

constexpr const char* kLogTag = "FFRS-GuestBoot";

/* One guest run. The thread owns the request and deletes it when it ends. */
struct GuestBootRequest {
    std::string image;
    std::string romfs;
    std::string log_path;
    unsigned long long quota = 0;
    unsigned long long stack = 0;
    unsigned long long resources = 0;
    std::vector<std::string> args;
};

pthread_mutex_t g_boot_lock = PTHREAD_MUTEX_INITIALIZER;
bool g_boot_started = false;

int CurrentThreadId() { return static_cast<int>(syscall(__NR_gettid)); }

/* Redirects this process's stdout and stderr into the requested file so the
 * guest boot transcript is kept verbatim instead of flooding the log
 * buffer. The original descriptors are restored when the guest ends. */
void RedirectOutput(const std::string& path) {
    if (path.empty()) return;
    fflush(stdout);
    fflush(stderr);
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
            "guest_boot log open failed path=%s; the transcript stays on stdout", path.c_str());
        return;
    }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

void* GuestThreadMain(void* opaque) {
    GuestBootRequest* request = static_cast<GuestBootRequest*>(opaque);
    int app_pid = getpid();
    int guest_tid = CurrentThreadId();

    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "guest_boot start app_pid=%d guest_tid=%d image=%s romfs=%s quota=%llu stack=%llu "
        "resources=%llu log=%s",
        app_pid, guest_tid, request->image.c_str(), request->romfs.c_str(),
        request->quota, request->stack, request->resources, request->log_path.c_str());

    /* The romfs root is explicit here. The runtime would otherwise fall back
     * to its candidate list, which is a host-side convenience. */
    if (!request->romfs.empty()) {
        setenv("NN_FS_ROM_PATH", request->romfs.c_str(), 1);
    }
    RedirectOutput(request->log_path);

    nso_boot_options options;
    options.path = request->image.c_str();
    options.quota = static_cast<size_t>(request->quota);
    options.stack = static_cast<size_t>(request->stack);
    options.resources = static_cast<size_t>(request->resources);

    std::vector<const char*> argv;
    argv.reserve(request->args.size());
    for (const std::string& value : request->args) argv.push_back(value.c_str());
    options.guest_argc = argv.size();
    options.guest_argv = argv.empty() ? nullptr : argv.data();

    int rc = nso_boot_run(&options);

    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "guest_boot finished rc=%d app_pid=%d guest_tid=%d", rc, app_pid, guest_tid);
    delete request;
    return nullptr;
}

std::string ToStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) return std::string();
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return std::string();
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_nativeStartGuest(JNIEnv* env, jclass,
                                                            jstring image, jstring romfs,
                                                            jstring log_path, jlong quota,
                                                            jlong stack, jlong resources,
                                                            jobjectArray guest_args) {
    pthread_mutex_lock(&g_boot_lock);
    if (g_boot_started) {
        pthread_mutex_unlock(&g_boot_lock);
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
            "guest_boot refused: a guest run is already active in this process");
        return -1;
    }
    g_boot_started = true;
    pthread_mutex_unlock(&g_boot_lock);

    GuestBootRequest* request = new (std::nothrow) GuestBootRequest();
    if (request == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "guest_boot request allocation failed");
        return -1;
    }
    request->image = ToStdString(env, image);
    request->romfs = ToStdString(env, romfs);
    request->log_path = ToStdString(env, log_path);
    request->quota = static_cast<unsigned long long>(quota);
    request->stack = static_cast<unsigned long long>(stack);
    request->resources = static_cast<unsigned long long>(resources);
    if (guest_args != nullptr) {
        jsize count = env->GetArrayLength(guest_args);
        for (jsize i = 0; i < count; i++) {
            jstring value = static_cast<jstring>(env->GetObjectArrayElement(guest_args, i));
            request->args.push_back(ToStdString(env, value));
            env->DeleteLocalRef(value);
        }
    }

    if (request->image.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
            "guest_boot refused: no guest image path was given");
        delete request;
        return -1;
    }

    /* The guest blocks for the whole session, so it never runs on the UI
     * thread that owns the SurfaceView. */
    pthread_t thread;
    if (pthread_create(&thread, nullptr, GuestThreadMain, request) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "guest_boot thread creation failed");
        delete request;
        return -1;
    }
    pthread_detach(thread);
    return static_cast<jlong>(thread);
}
