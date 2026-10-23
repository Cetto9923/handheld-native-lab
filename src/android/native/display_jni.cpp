#include "display_jni.h"

namespace ffrs {
DisplayInfo ReadDisplaySnapshot(JNIEnv* env, jobject snapshot) {
    DisplayInfo out;
    out.status = "unavailable_no_activity_snapshot";
    if (!snapshot) return out;
    jclass type = env->GetObjectClass(snapshot);
    if (!type) return out;
    auto read_int = [&](const char* name, int& value) {
        jfieldID field = env->GetFieldID(type, name, "I");
        if (!field) return false;
        value = env->GetIntField(snapshot, field);
        return !env->ExceptionCheck();
    };
    auto read_double = [&](const char* name, double& value) {
        jfieldID field = env->GetFieldID(type, name, "D");
        if (!field) return false;
        value = env->GetDoubleField(snapshot, field);
        return !env->ExceptionCheck();
    };
    auto read_string = [&](const char* name, std::string& value) {
        jfieldID field = env->GetFieldID(type, name, "Ljava/lang/String;");
        if (!field) return false;
        jstring text = static_cast<jstring>(env->GetObjectField(snapshot, field));
        if (!text) return !env->ExceptionCheck();
        const char* chars = env->GetStringUTFChars(text, nullptr);
        if (!chars) { env->DeleteLocalRef(text); return false; }
        value = chars;
        env->ReleaseStringUTFChars(text, chars);
        env->DeleteLocalRef(text);
        return true;
    };
    bool valid = read_int("width_px", out.width_px) && read_int("height_px", out.height_px)
        && read_int("content_width_px", out.content_width_px) && read_int("content_height_px", out.content_height_px)
        && read_int("inset_left_px", out.inset_left_px) && read_int("inset_top_px", out.inset_top_px)
        && read_int("inset_right_px", out.inset_right_px) && read_int("inset_bottom_px", out.inset_bottom_px)
        && read_int("display_id", out.display_id) && read_int("rotation", out.rotation)
        && read_int("current_mode_id", out.current_mode_id) && read_int("density_dpi", out.density_dpi)
        && read_double("density", out.density) && read_double("refresh_rate_hz", out.refresh_rate_hz)
        && read_string("size_source", out.size_source) && read_string("error", out.error)
        && read_string("insets_source", out.insets_source);
    jfieldID insets = valid ? env->GetFieldID(type, "insets_measured", "Z") : nullptr;
    if (insets) out.insets_measured = env->GetBooleanField(snapshot, insets);
    else valid = false;
    jfieldID timestamp = valid ? env->GetFieldID(type, "captured_elapsed_realtime_ns", "J") : nullptr;
    if (timestamp) {
        jlong value = env->GetLongField(snapshot, timestamp);
        out.captured_elapsed_realtime_ns = value > 0 ? static_cast<uint64_t>(value) : 0;
    } else valid = false;
    const char* names[] = {"mode_ids", "mode_widths", "mode_heights", "mode_refresh_rates"};
    jarray arrays[4] = {};
    jsize count = -1;
    if (valid) for (int index = 0; index < 4; index++) {
        jfieldID field = env->GetFieldID(type, names[index], index == 3 ? "[D" : "[I");
        if (!field) { valid = false; break; }
        arrays[index] = static_cast<jarray>(env->GetObjectField(snapshot, field));
        if (!arrays[index]) { valid = false; break; }
        jsize length = env->GetArrayLength(arrays[index]);
        if (count < 0) count = length;
        if (count != length || count > 1024) { valid = false; break; }
    }
    if (valid) {
        std::vector<jint> ids(count), widths(count), heights(count);
        std::vector<jdouble> rates(count);
        if (count) {
            env->GetIntArrayRegion(static_cast<jintArray>(arrays[0]), 0, count, ids.data());
            env->GetIntArrayRegion(static_cast<jintArray>(arrays[1]), 0, count, widths.data());
            env->GetIntArrayRegion(static_cast<jintArray>(arrays[2]), 0, count, heights.data());
            env->GetDoubleArrayRegion(static_cast<jdoubleArray>(arrays[3]), 0, count, rates.data());
        }
        valid = !env->ExceptionCheck();
        if (valid) for (jsize i = 0; i < count; i++) out.modes.push_back({ids[i], widths[i], heights[i], rates[i]});
    }
    for (auto array : arrays) if (array) env->DeleteLocalRef(array);
    env->DeleteLocalRef(type);
    if (!valid || env->ExceptionCheck()) {
        env->ExceptionClear();
        out = DisplayInfo{};
        out.status = "unavailable_invalid_activity_snapshot";
        out.error = "Activity display snapshot fields/arrays do not match the JNI interface.";
        return out;
    }
    return NormalizeDisplaySnapshot(out);
}
}
