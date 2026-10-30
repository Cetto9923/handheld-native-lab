// Minimal, dependency-free JSON emitter with deterministic field ordering.
//
// The probe emits machine-consumable capability JSON. Field order must be
// stable across runs and across the Android / host builds, so the writer never
// sorts: values are appended in the order the caller writes them.
//
// T03A (Android host + read-only device capability probe). Shared verbatim by
// the Android arm64-v8a build and the macOS host build.

#ifndef FFRS_JSON_WRITER_H
#define FFRS_JSON_WRITER_H

#include <string>
#include <vector>

namespace ffrs {

class JsonWriter {
public:
    JsonWriter();

    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();

    void Key(const char* key);

    void String(const char* value);
    void String(const std::string& value);
    void Bool(bool value);
    void Int(long long value);
    void UInt(unsigned long long value);
    void Double(double value);
    void Null();

    // Emits a raw pre-formatted JSON fragment (nested raw objects).
    void Raw(const std::string& fragment);

    const std::string& str() const { return out_; }

    static std::string Escape(const std::string& in);

private:
    void Sep();      // newline + indent + optional comma, before an element
    void ValueSep(); // same, but suppressed right after a key

    std::string out_;
    std::vector<bool> first_; // per open container: is the next element the first?
    bool after_key_ = false;
};

} // namespace ffrs

#endif // FFRS_JSON_WRITER_H
