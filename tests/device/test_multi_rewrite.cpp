#include "src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

struct xbg_shader_header {
    uint32_t magic,version,stage,binding_count,input_count,c1_bytes,spirv_bytes,reserved;
};
struct xbg_shader_binding {
    uint32_t binding,type,count,bytes,source,slot,sampler_slot,flags;
};

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    xbg_shader_header h;
    memcpy(&h, data.data(), sizeof(h));
    size_t offset = sizeof(h) + h.binding_count * sizeof(xbg_shader_binding) + h.input_count * 4 + h.c1_bytes;
    std::vector<uint32_t> words(h.spirv_bytes / 4);
    memcpy(words.data(), data.data() + offset, h.spirv_bytes);

    std::vector<uint32_t> bindings;
    for (int i = 2; i < argc; ++i) bindings.push_back(std::stoi(argv[i]));

    int total = 0;
    for (uint32_t b : bindings) {
        int r = xbg_spirv_integer_fetch(words, b, 1);
        std::cout << "rewrite binding " << b << " -> " << r << std::endl;
        if (r < 0) return 2;
        total += r;
    }
    std::cout << "Total rewrites: " << total << ", final words: " << words.size() << std::endl;

    std::ofstream out(".local/test_out.spv", std::ios::binary);
    out.write(reinterpret_cast<const char*>(words.data()), words.size() * 4);
    out.close();
    return 0;
}
