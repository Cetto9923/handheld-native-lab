#include "src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <iostream>
#include <fstream>
#include <vector>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::cerr << "Cannot open " << argv[1] << std::endl; return 2; }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    std::vector<uint32_t> words(bytes.size() / 4);
    memcpy(words.data(), bytes.data(), bytes.size());

    std::cout << "Original words: " << words.size() << std::endl;
    int r18 = xbg_spirv_integer_fetch(words, 18, 1);
    std::cout << "Rewrite binding 18: " << r18 << std::endl;
    int r30 = xbg_spirv_integer_fetch(words, 30, 1);
    std::cout << "Rewrite binding 30: " << r30 << std::endl;

    if (argc >= 3) {
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
        std::cout << "Saved to " << argv[2] << " (" << words.size() << " words)" << std::endl;
    }
    return 0;
}
