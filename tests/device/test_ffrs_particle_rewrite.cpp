#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"

#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

struct Instruction {
    uint32_t opcode;
    std::vector<uint32_t> operands;
};

struct ScalarType {
    uint32_t opcode;
    uint32_t width;
    uint32_t signedness;

    bool operator==(const ScalarType &other) const {
        return opcode == other.opcode && width == other.width &&
               signedness == other.signedness;
    }
};

static std::vector<uint32_t> load_module(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    assert(input.good());
    const auto bytes = input.tellg();
    assert(bytes >= 20 && bytes % 4 == 0);
    input.seekg(0);
    std::vector<uint32_t> words(size_t(bytes) / 4);
    input.read(reinterpret_cast<char *>(words.data()), bytes);
    assert(input.good() && words[0] == 0x07230203u);
    return words;
}

static std::map<uint32_t, ScalarType> buffer_sampled_types(
        const std::vector<uint32_t> &words) {
    std::map<uint32_t, Instruction> types;
    std::map<uint32_t, uint32_t> variables;
    std::map<uint32_t, uint32_t> bindings;
    for (size_t at = 5; at < words.size();) {
        const uint32_t count = words[at] >> 16;
        const uint32_t opcode = words[at] & 65535u;
        assert(count && count <= words.size() - at);
        if (opcode >= 19 && opcode <= 33) {
            types[words[at + 1]] = {
                opcode, {words.begin() + at + 2, words.begin() + at + count}
            };
        }
        if (opcode == 59) variables[words[at + 2]] = words[at + 1];
        if (opcode == 71 && count == 4 && words[at + 2] == 33)
            bindings[words[at + 1]] = words[at + 3];
        at += count;
    }

    std::map<uint32_t, ScalarType> result;
    for (const auto &binding : bindings) {
        const auto &pointer = types.at(variables.at(binding.first));
        assert(pointer.opcode == 32);
        const auto &sampled = types.at(pointer.operands.at(1));
        if (sampled.opcode != 27) continue;
        const auto &image = types.at(sampled.operands.at(0));
        assert(image.opcode == 25);
        if (image.operands.at(1) != 5) continue;  // Dim Buffer
        const auto &scalar = types.at(image.operands.at(0));
        assert(scalar.opcode == 21 || scalar.opcode == 22);
        const uint32_t signedness = scalar.opcode == 21 ? scalar.operands.at(1) : 0;
        result.emplace(binding.second, ScalarType{
            scalar.opcode, scalar.operands.at(0), signedness
        });
    }
    return result;
}

static unsigned count_fetches(const std::vector<uint32_t> &words) {
    unsigned result = 0;
    for (size_t at = 5; at < words.size();) {
        const uint32_t count = words[at] >> 16;
        assert(count && count <= words.size() - at);
        if ((words[at] & 65535u) == 95) ++result;
        at += count;
    }
    return result;
}

int main(int argc, char **argv) {
    assert(argc == 3);  // original module directory, output directory
    struct Case {
        const char *key;
        std::array<uint32_t, 2> integer_bindings;
        unsigned rewritten_fetches;
    };
    const Case cases[] = {
        {"9198ac8854711e1c", {12, 24}, 26},
        {"ead249f088784a89", {12, 24}, 26},
        {"cc083b4b242a59ba", {18, 30}, 33},
        {"d83a8836c4cfac88", {18, 30}, 24},
        {"8574fb4cb432cb74", {18, 30}, 29}
    };
    for (const auto &test : cases) {
        const auto original = load_module(std::string(argv[1]) + "/" + test.key + ".spv");
        const auto before = buffer_sampled_types(original);
        for (const auto &binding : before)
            assert((binding.second == ScalarType{22, 32, 0}));
        for (uint32_t binding : test.integer_bindings) assert(before.count(binding) == 1);

        auto unknown = original;
        assert(xbg_spirv_ffrs_stencil(unknown, 0, "unknown-particle-key") == 0);
        assert(unknown == original);
        auto wrong_stage = original;
        assert(xbg_spirv_ffrs_stencil(wrong_stage, 4, test.key) == 0);
        assert(wrong_stage == original);

        auto rewritten = original;
        const int changed = xbg_spirv_ffrs_stencil(rewritten, 0, test.key);
        assert(changed == int(test.rewritten_fetches));
        const auto after = buffer_sampled_types(rewritten);
        assert(after.size() == before.size());
        for (const auto &binding : before) {
            const bool integer = binding.first == test.integer_bindings[0] ||
                                 binding.first == test.integer_bindings[1];
            const ScalarType expected = integer ? ScalarType{21, 32, 0} : binding.second;
            assert(after.at(binding.first) == expected);
        }
        assert(count_fetches(rewritten) == count_fetches(original));

        const std::string output = std::string(argv[2]) + "/" + test.key + ".spv";
        std::ofstream stream(output, std::ios::binary);
        stream.write(reinterpret_cast<const char *>(rewritten.data()), rewritten.size() * 4);
        assert(stream.good());
        std::cout << test.key << " rewritten_fetches=" << changed
                  << " float_bindings_preserved=" << after.size() - 2
                  << " unknown_key_unchanged=1 wrong_stage_unchanged=1 PASS\n";
    }
}
