#include "input_jni.h"
#include "probe_core.h"

extern "C" JNIEXPORT jstring JNICALL
Java_org_ffrs_diagnostic_InputSnapshotJniTest_inspect(JNIEnv* env, jclass, jobject snapshot) {
    auto measured = ffrs::ReadInputSnapshot(env, snapshot);
    ffrs::ProbeContext context;
    context.input_snapshot = snapshot ? &measured : nullptr;
    ffrs::FullProbeReport report;
    ffrs::CollectInput(context, report.input);
    std::string json = ffrs::SerializeProbeReportToJson(report);
    return env->NewStringUTF(json.c_str());
}
