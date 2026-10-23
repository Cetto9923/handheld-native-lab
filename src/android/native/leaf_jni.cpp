#include "leaf_validation.h"
#include <android/log.h>
#include <jni.h>
#include <fstream>

namespace {
std::string Sha256(const std::vector<uint8_t>& bytes, void* context) {
    auto* env = static_cast<JNIEnv*>(context);
    jclass type = env->FindClass("java/security/MessageDigest");
    if (!type) { env->ExceptionClear(); return ""; }
    jmethodID factory = env->GetStaticMethodID(type, "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;");
    jmethodID digest = env->GetMethodID(type, "digest", "([B)[B");
    if (!factory || !digest || env->ExceptionCheck()) {
        env->ExceptionClear(); env->DeleteLocalRef(type); return "";
    }
    jstring algorithm = env->NewStringUTF("SHA-256");
    jobject instance = factory ? env->CallStaticObjectMethod(type, factory, algorithm) : nullptr;
    if (!instance || env->ExceptionCheck()) {
        env->ExceptionClear();
        if (algorithm) env->DeleteLocalRef(algorithm);
        env->DeleteLocalRef(type); return "";
    }
    jbyteArray input = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (input) env->SetByteArrayRegion(input, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    auto result = instance && digest && input && !env->ExceptionCheck()
        ? static_cast<jbyteArray>(env->CallObjectMethod(instance, digest, input)) : nullptr;
    std::string hex;
    if (!env->ExceptionCheck() && result && env->GetArrayLength(result) == 32) {
        jbyte value[32]; env->GetByteArrayRegion(result, 0, 32, value);
        constexpr char digits[] = "0123456789abcdef";
        for (jbyte byte : value) { unsigned v = static_cast<unsigned char>(byte); hex += digits[v >> 4]; hex += digits[v & 15]; }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (result) env->DeleteLocalRef(result);
    if (input) env->DeleteLocalRef(input);
    if (instance) env->DeleteLocalRef(instance);
    if (algorithm) env->DeleteLocalRef(algorithm);
    env->DeleteLocalRef(type);
    return hex;
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_ffrs_diagnostic_DiagnosticActivity_runNativeLeafDiagnostic(JNIEnv* env, jobject, jstring library, jstring destination) {
    const char* input = env->GetStringUTFChars(library, nullptr);
    if (!input) return nullptr;
    std::string path(input);
    env->ReleaseStringUTFChars(library, input);
    const char* output = env->GetStringUTFChars(destination, nullptr);
    if (!output) return nullptr;
    std::string report_path(output);
    env->ReleaseStringUTFChars(destination, output);
    const ffrs::LeafReport result = ffrs::VerifyLeafLibrary(path, Sha256, env);
    const std::string json = ffrs::SerializeLeafReport(result);
    std::ofstream report(report_path); report << json << '\n'; report.close();
    __android_log_print(ANDROID_LOG_INFO, "FFRS-Leaf", "status=%s samples=%u mismatches=%u report_write=%d game_startup=NOT_RUN",
                        result.status.c_str(), result.cases, result.mismatches, static_cast<bool>(report));
    return env->NewStringUTF(json.c_str());
}
