#include "json_writer.h"

#include <cmath>
#include <cstdio>

namespace ffrs {

JsonWriter::JsonWriter() { out_.reserve(16384); }

void JsonWriter::Sep() {
    if (first_.empty()) return;
    if (!first_.back()) out_ += ",";
    first_.back() = false;
    out_ += "\n";
    out_.append(2 * first_.size(), ' ');
}

void JsonWriter::ValueSep() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    Sep();
}

void JsonWriter::BeginObject() {
    ValueSep();
    out_ += "{";
    first_.push_back(true);
}

void JsonWriter::EndObject() {
    if (!first_.empty()) first_.pop_back();
    out_ += "}";
}

void JsonWriter::BeginArray() {
    ValueSep();
    out_ += "[";
    first_.push_back(true);
}

void JsonWriter::EndArray() {
    if (!first_.empty()) first_.pop_back();
    out_ += "]";
}

void JsonWriter::Key(const char* key) {
    Sep();
    out_ += "\"";
    out_ += Escape(key);
    out_ += "\": ";
    after_key_ = true;
}

void JsonWriter::String(const char* value) {
    ValueSep();
    if (value == nullptr) {
        out_ += "null";
        return;
    }
    out_ += "\"";
    out_ += Escape(value);
    out_ += "\"";
}

void JsonWriter::String(const std::string& value) { String(value.c_str()); }

void JsonWriter::Bool(bool value) {
    ValueSep();
    out_ += value ? "true" : "false";
}

void JsonWriter::Int(long long value) {
    ValueSep();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", value);
    out_ += buf;
}

void JsonWriter::UInt(unsigned long long value) {
    ValueSep();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", value);
    out_ += buf;
}

void JsonWriter::Double(double value) {
    ValueSep();
    if (std::isnan(value) || std::isinf(value)) {
        out_ += "null";
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", value);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last != std::string::npos && s[last] == '.') last++;
        s.erase(last + 1);
    }
    out_ += s;
}

void JsonWriter::Null() {
    ValueSep();
    out_ += "null";
}

void JsonWriter::Raw(const std::string& fragment) {
    ValueSep();
    out_ += fragment;
}

std::string JsonWriter::Escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

} // namespace ffrs
