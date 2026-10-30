#ifndef FFRS_LEAF_VALIDATION_H
#define FFRS_LEAF_VALIDATION_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ffrs {
using LeafFunction = uint32_t (*)(int32_t, int32_t, int32_t, uint32_t);
using LeafHasher = std::string (*)(const std::vector<uint8_t>&, void*);
inline constexpr char kLeafLibrarySha256[] = "8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8";
inline constexpr char kLeafCodeSha256[] = "8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9";
inline constexpr char kLeafModuleSha256[] = "6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5";
struct LeafReport {
    std::string status = "NOT_RUN";
    std::string stage = "input";
    std::string error;
    std::string library_sha256;
    std::string library_path;
    uint32_t cases = 0;
    uint32_t mismatches = 0;
    std::array<int64_t, 4> first_inputs {};
    uint32_t first_original = 0, first_library_reference = 0, first_local_reference = 0;
    bool code_executed = false;
};
// Rejects constructors, finalizers, dependencies and executable-entry ELF files.
bool ValidateLeafElf(const std::vector<uint8_t>& file, std::string& error);
// Pure typed-call comparison; host executions are never labelled device PASS.
LeafReport CompareLeafFunctions(LeafFunction original, LeafFunction library_reference);
LeafReport VerifyLeafLibrary(const std::string& path, LeafHasher sha256, void* hash_context);
std::string SerializeLeafReport(const LeafReport& report);
}
#endif
