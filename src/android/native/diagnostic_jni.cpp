#include "nso_diagnostic.h"
#include "probe_core.h"
#include "json_writer.h"
#include "display_jni.h"
#include "input_jni.h"
#include <android/log.h>
#include <jni.h>
#include <fstream>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_org_ffrs_diagnostic_DiagnosticActivity_runNativeDiagnostic(JNIEnv* env, jobject,
                                                               jstring module, jstring destination, jobject display, jobject input) {
    const char* module_chars = env->GetStringUTFChars(module, nullptr);
    if (!module_chars) return nullptr;
    std::string module_path(module_chars);
    env->ReleaseStringUTFChars(module, module_chars);
    const char* output_chars = env->GetStringUTFChars(destination, nullptr);
    if (!output_chars) return nullptr;
    std::string output_path(output_chars);
    env->ReleaseStringUTFChars(destination, output_chars);

    ffrs::ProbeContext context;
    ffrs::DisplayInfo measured_display = ffrs::ReadDisplaySnapshot(env, display);
    context.display_snapshot = &measured_display;
    ffrs::InputInfo measured_input = ffrs::ReadInputSnapshot(env, input);
    context.input_snapshot = &measured_input;
    context.execution_mode = "android_jni_diagnostic";
    ffrs::JsonWriter report;
    report.BeginObject();
    report.Key("schema"); report.String("ffrs.android.diagnostic.v1");
    report.Key("scope"); report.String("Native Android carrier and loader diagnostics; game startup and rendering NOT_RUN.");
    report.Key("capabilities"); report.Raw(ffrs::SerializeProbeReportToJson(ffrs::RunProbe(context)));
    report.Key("nso"); report.Raw(ffrs::DiagnoseNso(module_path));
    report.EndObject();
    std::ofstream output(output_path);
    output << report.str() << '\n';
    output.close();
    std::string result = report.str();
    if (!output) result += "\nReport write failed: " + output_path;
    __android_log_print(ANDROID_LOG_INFO, "FFRS-Diagnostic", "Diagnostic finished; report=%s write_ok=%d game_execution=NOT_RUN",
                        output_path.c_str(), static_cast<bool>(output));
    return env->NewStringUTF(result.c_str());
}
