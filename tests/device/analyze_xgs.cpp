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

void inspect_xgs(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cout << "Cannot open " << path << std::endl; return; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    if (data.size() < sizeof(xbg_shader_header)) return;
    xbg_shader_header h;
    memcpy(&h, data.data(), sizeof(h));
    std::cout << "=== " << path << " stage=" << h.stage << " bindings=" << h.binding_count
              << " spirv_bytes=" << h.spirv_bytes << " ===" << std::endl;
    size_t offset = sizeof(h);
    std::vector<xbg_shader_binding> bindings(h.binding_count);
    memcpy(bindings.data(), data.data() + offset, h.binding_count * sizeof(xbg_shader_binding));
    for (const auto &b : bindings) {
        if (b.type == 4 || b.type == 5) { // texel buffer
            std::cout << "  texel binding=" << b.binding << " type=" << b.type
                      << " slot=" << b.slot << " bytes=" << b.bytes << std::endl;
        }
    }
    offset += h.binding_count * sizeof(xbg_shader_binding) + h.input_count * 4 + h.c1_bytes;
    if (offset + h.spirv_bytes > data.size()) { std::cout << "Bad size\n"; return; }
    std::vector<uint32_t> words(h.spirv_bytes / 4);
    memcpy(words.data(), data.data() + offset, h.spirv_bytes);

    for (const auto &b : bindings) {
        if (b.type == 4) { // uniform texel buffer
            auto test_words = words;
            int r = xbg_spirv_integer_fetch(test_words, b.binding, 1);
            std::cout << "    test integer_fetch binding=" << b.binding << " -> result=" << r << std::endl;
        }
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) inspect_xgs(argv[i]);
    return 0;
}
