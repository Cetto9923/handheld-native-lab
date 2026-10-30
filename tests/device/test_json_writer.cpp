#include "../../src/android/native/json_writer.h"

#include <cassert>
#include <iostream>
#include <string>

void test_basic_json() {
    ffrs::JsonWriter w;
    w.BeginObject();
    w.Key("key1");
    w.String("value1");
    w.Key("number");
    w.Int(12345);
    w.Key("boolean");
    w.Bool(true);
    w.Key("floating");
    w.Double(3.14159);
    w.Key("list");
    w.BeginArray();
    w.Int(1);
    w.Int(2);
    w.Int(3);
    w.EndArray();
    w.EndObject();

    std::string s = w.str();
    assert(s.find("\"key1\": \"value1\"") != std::string::npos);
    assert(s.find("\"number\": 12345") != std::string::npos);
    assert(s.find("\"boolean\": true") != std::string::npos);
    assert(s.find("\"list\": [") != std::string::npos);
    std::cout << "[PASS] test_basic_json\n";
}

void test_escape_json() {
    ffrs::JsonWriter w;
    w.BeginObject();
    w.Key("escapes");
    w.String("line1\nline2\t\"quoted\"\\backslash\r\b\f");
    w.EndObject();

    std::string s = w.str();
    assert(s.find("\\n") != std::string::npos);
    assert(s.find("\\t") != std::string::npos);
    assert(s.find("\\\"quoted\\\"") != std::string::npos);
    assert(s.find("\\\\backslash") != std::string::npos);
    std::cout << "[PASS] test_escape_json\n";
}

int main() {
    test_basic_json();
    test_escape_json();
    std::cout << "All JsonWriter tests passed successfully.\n";
    return 0;
}
