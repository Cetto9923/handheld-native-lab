#include "input_jni.h"

namespace ffrs {
namespace {
// All local references are scoped to one object/array iteration. An invalid
// field stops further JNI calls until the entry point clears the exception.
class Reader {
    JNIEnv* env_;
    jobject object_;
    jclass type_;
    bool valid_;
    jfieldID field(const char* name, const char* signature) {
        if (!valid_) return nullptr;
        jfieldID id = env_->GetFieldID(type_, name, signature);
        valid_ = !env_->ExceptionCheck() && id;
        return id;
    }
    void check() { valid_ = valid_ && !env_->ExceptionCheck(); }
public:
    Reader(JNIEnv* env, jobject object) : env_(env), object_(object),
        type_(object ? env->GetObjectClass(object) : nullptr),
        valid_(type_ && !env->ExceptionCheck()) {}
    ~Reader() { if (type_) env_->DeleteLocalRef(type_); }
    bool good() const { return valid_; }
    int integer(const char* name) {
        jfieldID id = field(name, "I");
        int value = id ? env_->GetIntField(object_, id) : 0;
        check(); return value;
    }
    bool boolean(const char* name) {
        jfieldID id = field(name, "Z");
        bool value = id ? env_->GetBooleanField(object_, id) : false;
        check(); return value;
    }
    uint64_t timestamp(const char* name) {
        jfieldID id = field(name, "J");
        jlong value = id ? env_->GetLongField(object_, id) : 0;
        check(); return value > 0 ? static_cast<uint64_t>(value) : 0;
    }
    double number(const char* name) {
        jfieldID id = field(name, "D");
        double value = id ? env_->GetDoubleField(object_, id) : 0;
        check(); return value;
    }
    std::string string(const char* name) {
        jfieldID id = field(name, "Ljava/lang/String;");
        jstring text = id ? static_cast<jstring>(env_->GetObjectField(object_, id)) : nullptr;
        check();
        std::string value;
        if (text && valid_) {
            const char* chars = env_->GetStringUTFChars(text, nullptr);
            check();
            if (chars) { value = chars; env_->ReleaseStringUTFChars(text, chars); }
            else valid_ = false;
        }
        if (text) env_->DeleteLocalRef(text);
        return value;
    }
    std::vector<int> integers(const char* name) {
        jfieldID id = field(name, "[I");
        auto array = id ? static_cast<jintArray>(env_->GetObjectField(object_, id)) : nullptr;
        check();
        std::vector<jint> values;
        if (valid_ && array) {
            jsize count = env_->GetArrayLength(array); check();
            if (count < 0 || count > 4096) valid_ = false;
            if (valid_) {
                values.resize(count);
                if (count) { env_->GetIntArrayRegion(array, 0, count, values.data()); check(); }
            }
        } else valid_ = false;
        if (array) env_->DeleteLocalRef(array);
        return {values.begin(), values.end()};
    }
    template<class Function>
    void objects(const char* name, const char* signature, Function consume) {
        jfieldID id = field(name, signature);
        auto array = id ? static_cast<jobjectArray>(env_->GetObjectField(object_, id)) : nullptr;
        check();
        if (valid_ && array) {
            jsize count = env_->GetArrayLength(array); check();
            if (count < 0 || count > 256) valid_ = false;
            for (jsize index = 0; valid_ && index < count; index++) {
                jobject child = env_->GetObjectArrayElement(array, index); check();
                if (child && valid_) valid_ = consume(child);
                else valid_ = false;
                if (child) env_->DeleteLocalRef(child);
            }
        } else valid_ = false;
        if (array) env_->DeleteLocalRef(array);
    }
};
}

InputInfo ReadInputSnapshot(JNIEnv* env, jobject snapshot) {
    InputInfo out;
    out.status = "unavailable_no_android_api_snapshot";
    if (!snapshot) return out;
    bool valid;
    {
        Reader reader(env, snapshot);
        out.source = reader.string("source");
        out.error = reader.string("error");
        out.measured = reader.boolean("measured");
        out.enumeration_complete = reader.boolean("enumeration_complete");
        out.captured_elapsed_realtime_ns = reader.timestamp("captured_elapsed_realtime_ns");
        reader.objects("devices", "[Lorg/ffrs/diagnostic/InputSnapshot$Device;", [&](jobject object) {
            Reader device(env, object);
            InputDeviceInfo d;
            d.id = device.integer("id"); d.name = device.string("name");
            d.vendor_id = device.integer("vendor_id"); d.product_id = device.integer("product_id");
            d.controller_number = device.integer("controller_number");
            d.source_mask = static_cast<uint32_t>(device.integer("source_mask"));
            d.is_virtual = device.boolean("is_virtual"); d.key_codes = device.integers("key_codes");
            device.objects("axes", "[Lorg/ffrs/diagnostic/InputSnapshot$Axis;", [&](jobject object) {
                Reader axis(env, object);
                AxisInfo a;
                a.type = axis.integer("type"); a.name = axis.string("name");
                a.source_mask = static_cast<uint32_t>(axis.integer("source_mask"));
                a.min = axis.number("min"); a.max = axis.number("max");
                a.flat = axis.number("flat"); a.fuzz = axis.number("fuzz");
                a.resolution = axis.number("resolution");
                if (axis.good()) d.axes.push_back(a);
                return axis.good();
            });
            if (device.good()) out.devices.push_back(d);
            return device.good();
        });
        valid = reader.good();
    }
    if (!valid || env->ExceptionCheck()) {
        env->ExceptionClear();
        out = InputInfo{};
        out.status = "unavailable_invalid_input_snapshot";
        out.error = "Input snapshot fields/arrays do not match the typed JNI interface.";
        return out;
    }
    return NormalizeInputSnapshot(out);
}
}
