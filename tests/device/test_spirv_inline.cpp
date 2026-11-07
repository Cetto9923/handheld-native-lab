// Real-input component probe. Test inputs/outputs stay in .local, not Git.
#include "xbg_spirv_inline.h"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    std::vector<uint32_t> invalid;
    if (xbg_inline_subgroup_helpers(invalid) != XbgInlineResult::Invalid) return 3;
    invalid = {0x07230203u, 0x00010300u, 0, 1, 0, 0};
    if (xbg_inline_subgroup_helpers(invalid) != XbgInlineResult::Invalid) return 4;
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) return 5;
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < 20 || bytes.size() % 4) return 6;
    std::vector<uint32_t> words(bytes.size() / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        const auto *p = reinterpret_cast<const unsigned char *>(bytes.data() + i * 4);
        words[i] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    if (xbg_inline_subgroup_helpers(words) != XbgInlineResult::Changed) return 7;
    const auto first = words;
    if (xbg_inline_subgroup_helpers(words) != XbgInlineResult::Unchanged || first != words) return 8;
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(words.data()), words.size() * sizeof(uint32_t));
    return out ? 0 : 9;
}
