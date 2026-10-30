#include "display_jni.h"
#include "probe_core.h"

extern "C" JNIEXPORT jstring JNICALL
Java_org_ffrs_diagnostic_DisplaySnapshotJniTest_inspect(JNIEnv* env, jclass, jobject snapshot) {
    auto measured = ffrs::ReadDisplaySnapshot(env, snapshot);
    ffrs::ProbeContext context;
    context.display_snapshot = snapshot ? &measured : nullptr;
    ffrs::FullProbeReport report;
    ffrs::CollectDisplay(context, report.display);
    std::string json = ffrs::SerializeProbeReportToJson(report);
    return env->NewStringUTF(json.c_str());
}
