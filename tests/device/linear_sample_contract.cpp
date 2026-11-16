#include "xbg_shader_identity.h"
#include "xbg_spirv_linear_sample.h"
#include <cassert>
#include <fstream>
#include <iterator>

static std::vector<uint32_t> read(const char *path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    assert(bytes.size() % 4 == 0);
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    auto original = read(argv[1]);
    auto changed = original;
    const uint32_t binding = uint32_t(std::stoul(argv[3]));
    auto candidates = xbg_spirv_linear_sample_candidates(original);
    assert(std::find(candidates.begin(), candidates.end(), binding) != candidates.end());
    assert(xbg_spirv_linear_sample(changed, {binding}));
    assert(changed != original);
    assert(xbg_spirv_filter_independent(changed).count(binding));
    const auto stable = changed;
    assert(!xbg_spirv_linear_sample(changed, {binding}));
    assert(changed == stable);
    changed = original;
    assert(!xbg_spirv_linear_sample(changed, {UINT32_MAX}));
    assert(changed == original);
    changed = original;
    changed.push_back(0);
    const auto malformed = changed;
    assert(!xbg_spirv_linear_sample(changed, {binding}));
    assert(changed == malformed);
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char *>(stable.data()), stable.size() * 4);
    using Formats = std::vector<std::pair<uint32_t, uint32_t>>;
    std::string base = "old";
    Formats integer, typed, vertex, linear;
    const std::string identity = "0123456789abcdef:ib1:1_98:stb1:2_100:vin1:0_2:lf1:52_100,106_100";
    assert(xbg_shader_identity(identity, base, integer, &typed, &vertex, &linear));
    assert((linear == Formats{{52, 100}, {106, 100}}));
    assert(!xbg_shader_identity(identity, base, integer, &typed, &vertex));
    for (const char *suffix :
         {":lf1:52_103", ":lf1:52_100,52_100", ":lf1:052_100", ":lf1:52_100:vin1:0_2"}) {
        const std::string before = base;
        const Formats saved = linear;
        assert(!xbg_shader_identity(std::string("0123456789abcdef") + suffix, base, integer, &typed,
                                    &vertex, &linear));
        assert(base == before && linear == saved);
    }
}
