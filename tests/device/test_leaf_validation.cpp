#include "leaf_validation.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <tuple>

static std::set<std::tuple<int32_t, int32_t, int32_t, uint32_t>> seen;
static uint32_t Oracle(int32_t a, int32_t b, int32_t c, uint32_t flags) {
    int32_t top = a > b ? a : b;
    const int32_t third = flags & 1 ? c : 1;
    if (third > top) top = third;
    uint32_t value = static_cast<uint32_t>(top) | 1u, width = 0;
    do { ++width; value /= 2; } while (value);
    return width;
}
static uint32_t Recording(int32_t a, int32_t b, int32_t c, uint32_t f) {
    seen.emplace(a, b, c, f); return Oracle(a, b, c, f);
}
static uint32_t Wrong(int32_t a, int32_t b, int32_t c, uint32_t f) {
    return Oracle(a, b, c, f) + 1;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    auto okay = ffrs::CompareLeafFunctions(Recording, Oracle);
    assert(okay.cases == 16875 && okay.mismatches == 0 && seen.size() == 16875);
    assert(okay.status == "HOST_TEST_COMPARISON_ONLY");
    assert(seen.count({INT32_MIN, INT32_MAX, -1, UINT32_MAX}));
    auto wrong_original = ffrs::CompareLeafFunctions(Wrong, Oracle);
    assert(wrong_original.status == "FAIL" && wrong_original.mismatches == 16875);
    auto wrong_reference = ffrs::CompareLeafFunctions(Oracle, Wrong);
    assert(wrong_reference.status == "FAIL" && wrong_reference.mismatches == 16875);
    auto absent_symbol = ffrs::CompareLeafFunctions(nullptr, Oracle);
    assert(absent_symbol.status == "FAIL" && absent_symbol.cases == 0);
    auto missing = ffrs::VerifyLeafLibrary("/ffrs-intentionally-missing/leaf.so", nullptr, nullptr);
    assert(missing.status == "NOT_RUN" && !missing.code_executed);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(input)), {});
    auto unverified = ffrs::VerifyLeafLibrary(argv[1], nullptr, nullptr);
    assert(unverified.status == "FAIL" && !unverified.code_executed);
    std::string error;
    assert(ffrs::ValidateLeafElf(file, error)); // Parses bytes only: never loads a game library on host.
    auto malformed = file; malformed.resize(10);
    assert(!ffrs::ValidateLeafElf(malformed, error));
    malformed = file; malformed[24] = 1;
    assert(!ffrs::ValidateLeafElf(malformed, error));
    // Reviewed .dynamic starts at file offset 0x2f8. Inject DT_INIT/NEEDED.
    for (uint8_t tag : {uint8_t(12), uint8_t(1), uint8_t(25), uint8_t(26)}) {
        malformed = file; malformed[0x2f8] = tag;
        assert(!ffrs::ValidateLeafElf(malformed, error));
    }
    std::cout << "HOST_TEST_ONLY: 16875 unique synthetic input comparisons; mismatch, missing and ELF preflight rejection checks passed. Android original leaf NOT_RUN.\n";
}
