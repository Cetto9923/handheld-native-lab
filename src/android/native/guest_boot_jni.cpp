// Starts the owned guest inside the process that owns the game window.
//
// The window is a process-local platform handle: an ANativeWindow that the
// Activity published cannot be handed to a guest running in another process.
// The guest therefore runs here, on its own thread inside the app process,
// through the same boot code the standalone CLI uses (src/runtime/nso/
// nso_boot.c). Nothing in this file fabricates a frame: it starts the guest
// and reports what the guest actually did.

#include <android/log.h>
#include "../../graphics/nvn_device/nvn_engine.h"
#include "../../graphics/nvn_device/nvn_memory_pool.h"
#include <jni.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <new>

extern "C" {
#include "nso_boot.h"
#include "nso_host.h"
#include "sdk_component.h"
#include "movie_decoder.h"
#include "audio_bridge.h"
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
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
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
    setenv("DM_GFX_COMPLETION_WAIT", "1", 1);
    setenv("DM_GFX_WB_SKIP", "1", 1);
    setenv("DM_GFX_NOLOAD", "1", 1);
    setenv("DM_GFX_PIPE_MEMO", "1", 1);
    setenv("DM_GFX_CLEAR_OVERWRITE", "1", 1);
    RedirectOutput(request->log_path);
    {
        std::string path = request->log_path;
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return nullptr;
        path.resize(slash);
        // Each APK owns its port configuration. Do not inherit the main
        // package's docked profile when this runtime runs in an isolated APK.
        // RG477M's fixed handheld profile is the 720p baseline; this is not
        // the Android CPU/GPU game-mode switch and does not change game time.
        const std::string operation_marker = path + "/oe-operation-mode";
        int operation_mode = 0;
        const char *operation_source = "default-handheld";
        FILE *operation_file = fopen(operation_marker.c_str(), "rb");
        if (operation_file) {
            int value = fgetc(operation_file);
            fclose(operation_file);
            if (value == '0' || value == '1') {
                operation_mode = value - '0';
                operation_source = "app-marker";
            } else fprintf(stderr, "[port] invalid operation-mode marker; using handheld\n");
        }
        setenv("FFRS_OPERATION_MODE", operation_mode ? "1" : "0", 1);
        fprintf(stderr, "[port] operation_mode=%d source=%s marker=%s\n",
                operation_mode, operation_source, operation_marker.c_str());
        const std::string marker = path + "/gfx-codex-movie-dump";
        FILE *enabled = fopen(marker.c_str(), "rb");
        const bool dump_movie = enabled && fgetc(enabled) == '1';
        if (enabled) fclose(enabled);
        if (dump_movie) {
            std::string dump = path + "/codex-movie-" + std::to_string(getpid());
            if (!mkdir(dump.c_str(), 0700)) {
                fprintf(stderr, "[movie] bounded diagnostic directory=%s result=%d\n", dump.c_str(),
                        ffrs_movie_decoder_set_diagnostics(dump.c_str()));
            } else fprintf(stderr, "[movie] diagnostic mkdir failed; decoding remains unchanged\n");
        }
        const std::string audio_marker = path + "/gfx-codex-audio-dump";
        ffrs_audio_set_platform_directory(path.c_str());
        enabled = fopen(audio_marker.c_str(), "rb");
        const bool dump_audio = enabled && fgetc(enabled) == '1';
        if (enabled) fclose(enabled);
        if (dump_audio) {
            const std::string dump = path + "/codex-audio-" + std::to_string(getpid());
            if (!mkdir(dump.c_str(), 0700))
                fprintf(stderr, "[audio] bounded diagnostic directory=%s result=%d\n", dump.c_str(),
                        ffrs_audio_set_diagnostics(dump.c_str()));
        }
        /* sdk.nso: prefer the one shipped next to main.nso in the game folder (so a phone-side install only needs to
         * copy one folder), otherwise the app files directory as before. */
        std::string sdk = path + "/sdk.nso";
        {
            const std::string img = request->image;
            const size_t islash = img.find_last_of('/');
            if (islash != std::string::npos) {
                const std::string beside = img.substr(0, islash) + "/sdk.nso";
                if (access(beside.c_str(), R_OK) == 0) sdk = beside;
            }
        }
        fprintf(stderr, "[sdk-component] using %s\n", sdk.c_str());
        if (ffrs_sdk_component_load(sdk.c_str())) {
            fprintf(stderr, "[sdk-component] startup refused; verified SDK component missing or invalid\n");
            return nullptr;
        }
    }

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

static jlong StartGuest(JNIEnv* env,
                                                            jstring image, jstring romfs,
                                                            jstring log_path, jlong quota,
                                                            jlong stack, jlong resources,
                                                            jobjectArray guest_args, jstring save_root) {
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
    {
        std::string save = ToStdString(env, save_root);
        if (save.empty()) {
            save = request->log_path;
            size_t slash = save.find_last_of('/');
            if (slash == std::string::npos) { delete request; return -1; }
            save.resize(slash);
            save += "/codex-local-save";
        }
        if (nso_host_fs_set_save_root(save.c_str())) return -1;
    }
    {
        /* The shader bundles live beside the guest's log: the engine reads
         * the bundle of a shader by the name of that shader, and the name is
         * the hash of the control section of the guest's own shader. */
        std::string shaders = request->log_path;
        size_t slash = shaders.find_last_of('/');
        if (slash != std::string::npos) shaders.resize(slash);
        shaders += "/shaders";
        {
            /* A phone-side install ships the shader bundles inside the game folder (next to main.nso). The engine keeps
             * deriving its marker / override files from the parent of the shader directory, so that directory stays in
             * the app files folder; the shipped bundles are only a fallback (FFRS_SHADER_ALT) when a bundle is not there. */
            const std::string img = request->image;
            const size_t islash = img.find_last_of('/');
            if (islash != std::string::npos) {
                const std::string beside = img.substr(0, islash) + "/shaders";
                struct stat sb;
                if (stat(beside.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
                    setenv("FFRS_SHADER_ALT", beside.c_str(), 1);
                    fprintf(stderr, "[shaders] fallback directory %s\n", beside.c_str());
                }
            }
        }
        // Managed installer enables this only when the trusted data contract names the archive.
        unsetenv("FFRS_SHADER_ARCHIVE");
        std::string marker = shaders.substr(0, shaders.size() - strlen("/shaders")) + "/gfx-shader-archive";
        FILE *enabled = fopen(marker.c_str(), "rb");
        bool use_archive = enabled && fgetc(enabled) == '1';
        if (enabled) fclose(enabled);
        if (use_archive) {
            size_t slash = request->image.find_last_of('/');
            if (slash != std::string::npos) {
                std::string archive = request->image.substr(0, slash) + "/shaders.hnla";
                setenv("FFRS_SHADER_ARCHIVE", archive.c_str(), 1);
            }
        }
        nvn_engine_set_shader_directory(shaders.c_str());
    }
    {
        /* The flags of a run are a file beside the guest's log. They are a
         * diagnostic and not a mode of operation: they say which part of the
         * graphics path this run is meant to exercise. */
        std::string flags = request->log_path;
        size_t slash = flags.find_last_of('/');
        if (slash != std::string::npos) flags.resize(slash);
        flags += "/ffrs.flags";
        FILE* file = fopen(flags.c_str(), "r");
        if (file != nullptr) {
            char line[256];
            while (fgets(line, sizeof(line), file) != nullptr) {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                if (text == "noengine") {
                    nvn_engine_set_enabled(0);
                    __android_log_print(ANDROID_LOG_INFO, kLogTag, "flags: the engine is switched off");
                } else if (text == "syntheticaddr") {
                    nvn_memory_pool_set_synthetic_address(1);
                    __android_log_print(ANDROID_LOG_INFO, kLogTag, "flags: the buffer address is the old one");
                }
            }
            fclose(file);
        }
    }
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

// Preserve the existing ABI for all frozen APKs. Managed installs supply a
// stable app-owned save path independently of the replaceable data version.
extern "C" JNIEXPORT jlong JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_nativeStartGuest(JNIEnv* env, jclass,
        jstring image, jstring romfs, jstring log_path, jlong quota,
        jlong stack, jlong resources, jobjectArray guest_args) {
    return StartGuest(env, image, romfs, log_path, quota, stack, resources, guest_args, nullptr);
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_ffrs_diagnostic_GameWindowNative_nativeStartInstalledGuest(JNIEnv* env, jclass,
        jstring image, jstring romfs, jstring log_path, jlong quota,
        jlong stack, jlong resources, jobjectArray guest_args, jstring save_root) {
    if (save_root == nullptr || ToStdString(env, save_root).empty()) return -1;
    return StartGuest(env, image, romfs, log_path, quota, stack, resources, guest_args, save_root);
}
