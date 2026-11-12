// Offline coverage analyzer for the RG477M typed-storage rewrite.
//
// It links the shipping headers (xbg_spirv_typed_storage.h, xbg_spirv_stencil.h)
// so every verdict comes from the code that runs on the device, not from a
// second implementation of the rules.
//
// Input  : a length-prefixed stream of shader bundles on stdin.
//   record := u32 keyLen | key bytes | u32 bundleLen | bundle bytes
//   bundle := the game's .xgs container (XGS1 header, bindings, inputs, c1, SPIR-V)
// Output : one JSON object per record on stdout.
//
// For every storage descriptor the package declares, the analyzer reports:
//   * what the module itself declares for that binding (Unknown = formatless),
//   * whether the module actually writes/reads that image,
//   * for every SPIR-V-expressible bound VkFormat, whether the shipping typed
//     storage rewrite accepts the module,
//   * why the rewrite refuses when it refuses.
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h"
#include "../../src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Vulkan descriptor types used by the package (vulkan_core.h).
constexpr uint32_t VK_DT_STORAGE_IMAGE = 3;
constexpr uint32_t VK_DT_STORAGE_TEXEL_BUFFER = 5;

// The VkFormats the shipping format table can express.
constexpr uint32_t CANDIDATE_FORMATS[] = {98, 99, 100, 101, 102, 103, 107, 108, 109};

// SPIR-V opcode numbers used below (SPIR-V 1.3 core).
constexpr uint32_t OP_TYPE_INT = 21;
constexpr uint32_t OP_TYPE_FLOAT = 22;
constexpr uint32_t OP_TYPE_POINTER = 32;
constexpr uint32_t OP_VARIABLE = 59;
constexpr uint32_t OP_LOAD = 61;
constexpr uint32_t OP_IMAGE_FETCH = 95;
constexpr uint32_t OP_IMAGE_READ = 98;
constexpr uint32_t OP_IMAGE_WRITE = 99;
constexpr uint32_t OP_IMAGE_SPARSE_READ = 305;

struct PackageBinding {
    uint32_t binding = 0, type = 0, count = 0, bytes = 0;
    uint32_t source = 0, slot = 0, sampler_slot = 0, flags = 0;
};

struct Bundle {
    uint32_t stage = 0;
    std::vector<PackageBinding> bindings;
    std::vector<uint32_t> words;
    bool valid = false;
    std::string error;
};

bool parse_bundle(const std::vector<uint8_t> &bytes, Bundle &out){
    if(bytes.size() < 32){ out.error = "bundle shorter than the header"; return false; }
    uint32_t header[8];
    memcpy(header, bytes.data(), sizeof(header));
    const uint32_t magic = header[0], version = header[1];
    out.stage = header[2];
    const uint32_t binding_count = header[3], input_count = header[4];
    const uint32_t c1_bytes = header[5], spirv_bytes = header[6];
    if(magic != 0x31534758u || version != 1u){ out.error = "not an XGS1 v1 bundle"; return false; }
    if(binding_count > 256 || input_count > 16 || c1_bytes > 65536 || spirv_bytes % 4){
        out.error = "bundle header out of range"; return false;
    }
    size_t offset = 32;
    const size_t expected = offset + size_t(binding_count) * 32 + size_t(input_count) * 4
                          + c1_bytes + spirv_bytes;
    if(expected != bytes.size()){ out.error = "bundle extent mismatch"; return false; }
    out.bindings.resize(binding_count);
    for(uint32_t i = 0; i < binding_count; ++i){
        uint32_t *slot = &out.bindings[i].binding;
        memcpy(slot, bytes.data() + offset, 32);
        offset += 32;
    }
    offset += size_t(input_count) * 4;
    offset += c1_bytes;
    out.words.resize(spirv_bytes / 4);
    memcpy(out.words.data(), bytes.data() + offset, spirv_bytes);
    out.valid = true;
    return true;
}

// The result-id to instruction-offset map the shipping code builds.
void build_defs(const std::vector<uint32_t> &w, std::unordered_map<uint32_t, size_t> &def){
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) return;
        if(op >= 19 && op <= 33 && n >= 2) def[w[i + 1]] = i;
        if(op == OP_VARIABLE && n >= 4) def[w[i + 2]] = i;
        i += n;
    }
}

// The ids of the values produced by loading the descriptor variable.
std::vector<uint32_t> loaded_ids(const std::vector<uint32_t> &w, uint32_t variable){
    std::vector<uint32_t> out;
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) break;
        if(op == OP_LOAD && n >= 4 && w[i + 3] == variable) out.push_back(w[i + 2]);
        i += n;
    }
    return out;
}

// Does the module write / read the image behind this descriptor variable?
struct ImageUsage { int writes = 0, reads = 0, fetches = 0; };
ImageUsage image_usage(const std::vector<uint32_t> &w, uint32_t variable){
    ImageUsage usage;
    const std::vector<uint32_t> loaded = loaded_ids(w, variable);
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) break;
        if(op == OP_IMAGE_WRITE && n >= 4){
            for(uint32_t id : loaded) if(w[i + 1] == id){ ++usage.writes; break; }
        } else if(op == OP_IMAGE_READ && n >= 5){
            for(uint32_t id : loaded) if(w[i + 3] == id){ ++usage.reads; break; }
        } else if(op == OP_IMAGE_SPARSE_READ && n >= 5){
            for(uint32_t id : loaded) if(w[i + 3] == id){ ++usage.reads; break; }
        } else if(op == OP_IMAGE_FETCH && n >= 5){
            for(uint32_t id : loaded) if(w[i + 3] == id){ ++usage.fetches; break; }
        }
        i += n;
    }
    return usage;
}

// The capability list of the module (OpCapability, opcode 17).
std::vector<uint32_t> capabilities(const std::vector<uint32_t> &w){
    std::vector<uint32_t> out;
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) break;
        if(op == 17 && n >= 2) out.push_back(w[i + 1]);
        i += n;
    }
    return out;
}

// The scalar class of an image's Sampled Type: 0 float, 1 signed, 2 unsigned.
// Mirrors the shipping check in xbg_typed_storage_rewrite.
int sampled_class(const std::vector<uint32_t> &w, uint32_t sampled_type){
    std::unordered_map<uint32_t, size_t> def;
    build_defs(w, def);
    const auto found = def.find(sampled_type);
    if(found == def.end()) return -1;
    const size_t at = found->second;
    const uint32_t op = w[at] & 0xffffu;
    if(op == OP_TYPE_FLOAT) return 0;
    if(op == OP_TYPE_INT) return w[at + 3] == 0 ? 2 : 1;
    return -1;
}

// A diagnostic mirror of the shipping refusal checks, used only to explain a
// refusal. The shipping function stays authoritative; the analyzer asserts
// that this explanation agrees with it on every case.
std::string diagnose(const std::vector<uint32_t> &w, uint32_t binding, uint32_t spirv_format){
    const uint32_t scalar = xbg_spirv_image_format_scalar(spirv_format);
    if(scalar > 2) return "invalid-target-format";
    if(w.size() < 5 || w[0] != 0x07230203u) return "malformed-module";
    XbgStorageImageBinding info;
    if(!xbg_spirv_binding_image(w, binding, info)) return "no-storage-image-for-binding";
    if(info.declared_format != XBG_IMAGE_FORMAT_UNKNOWN)
        return info.declared_format == spirv_format ? "already-typed" : "typed-with-other-format";
    std::unordered_map<uint32_t, size_t> def;
    build_defs(w, def);
    // The image type must belong to this descriptor alone.
    uint32_t users = 0;
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) break;
        if(op == OP_VARIABLE && n >= 4){
            const auto ptr = def.find(w[i + 1]);
            if(ptr != def.end() && (w[ptr->second] & 0xffffu) == OP_TYPE_POINTER
               && w[ptr->second + 3] == info.image_type) ++users;
        }
        i += n;
    }
    if(users != 1) return "image-type-shared-by-multiple-descriptors";
    const int declared = sampled_class(w, info.sampled_type);
    if(declared < 0) return "unknown-sampled-type";
    if(declared != int(scalar)) return "sampled-type-class-mismatch";
    // A formatless read on the same image keeps the module formatless.
    const std::vector<uint32_t> loaded = loaded_ids(w, info.variable);
    for(size_t i = 5; i < w.size();){
        const uint32_t n = w[i] >> 16, op = w[i] & 0xffffu;
        if(!n || i + n > w.size()) break;
        if((op == OP_IMAGE_READ || op == OP_IMAGE_SPARSE_READ) && n >= 4){
            for(uint32_t id : loaded) if(w[i + 3] == id) return "formatless-read-on-same-image";
        }
        i += n;
    }
    return "accepted";
}

// Optional dump directory: when it is set, every accepted rewrite is written
// as <key>_<binding>_<vkformat>.spv so that spirv-val can check the result.
std::string g_dump_dir;

bool dump_rewritten(const std::string &key, uint32_t binding, uint32_t vk_format,
                    const std::vector<uint32_t> &words){
    if(g_dump_dir.empty()) return false;
    const std::string path = g_dump_dir + "/" + key + "_" + std::to_string(binding)
                           + "_" + std::to_string(vk_format) + ".spv";
    FILE *file = fopen(path.c_str(), "wb");
    if(!file) return false;
    const size_t written = fwrite(words.data(), 4, words.size(), file);
    fclose(file);
    return written == words.size();
}

// The module-level variant gets its own file name.
bool dump_module_variant(const std::string &key, const std::vector<uint32_t> &words){
    if(g_dump_dir.empty()) return false;
    const std::string path = g_dump_dir + "/" + key + "_variant.spv";
    FILE *file = fopen(path.c_str(), "wb");
    if(!file) return false;
    const size_t written = fwrite(words.data(), 4, words.size(), file);
    fclose(file);
    return written == words.size();
}

std::string json_escape(const std::string &in){
    std::string out;
    for(char c : in){
        if(c == '"' || c == '\\'){ out += '\\'; out += c; }
        else out += c;
    }
    return out;
}

} // namespace

int main(int argc, char **argv){
    if(argc > 1){
        g_dump_dir = argv[1];
        std::error_code ec;
        std::filesystem::create_directories(g_dump_dir, ec);
        if(ec){ fprintf(stderr, "cannot create dump directory %s: %s\n", g_dump_dir.c_str(), ec.message().c_str()); return 2; }
    }
    auto read_u32 = [&](uint32_t &value)->bool{
        uint8_t raw[4];
        size_t got = 0;
        while(got < 4){
            const size_t n = fread(raw + got, 1, 4 - got, stdin);
            if(!n) return false;
            got += n;
        }
        memcpy(&value, raw, 4);
        return true;
    };
    size_t mismatches = 0;
    for(;;){
        uint32_t key_len = 0, bundle_len = 0;
        if(!read_u32(key_len)) break;
        std::string key(key_len, 0);
        if(key_len && fread(&key[0], 1, key_len, stdin) != key_len) break;
        if(!read_u32(bundle_len)) break;
        std::vector<uint8_t> bytes(bundle_len);
        if(bundle_len && fread(bytes.data(), 1, bundle_len, stdin) != bundle_len) break;

        Bundle bundle;
        const bool parsed = parse_bundle(bytes, bundle);
        std::string out = "{\"key\":\"" + json_escape(key) + "\"";
        if(!parsed){
            out += ",\"parsed\":false,\"error\":\"" + json_escape(bundle.error) + "\"}";
            printf("%s\n", out.c_str());
            fflush(stdout);
            continue;
        }
        out += ",\"parsed\":true,\"stage\":" + std::to_string(bundle.stage);

        // The engine rewrites the module before it records the module's own
        // image form. Only the stencil rewrite touches image declarations.
        std::vector<uint32_t> words = bundle.words;
        const int stencil = xbg_spirv_ffrs_stencil(words, bundle.stage, key.c_str());
        out += ",\"stencil_result\":" + std::to_string(stencil);

        // After a successful stencil rewrite the engine overwrites the
        // package's scalar field from the proven integer fact.
        std::vector<PackageBinding> bindings = bundle.bindings;
        if(stencil > 0){
            for(auto &binding : bindings){
                uint32_t scalar = xbg_ffrs_integer_fact(bundle.stage, key.c_str(), binding.binding);
                if(binding.binding == xbg_ffrs_uint_binding(bundle.stage, key.c_str())
                   || binding.binding == xbg_ffrs_uint_store_binding(bundle.stage, key.c_str()))
                    scalar = 1;
                if(scalar) binding.flags = (binding.flags & ~(3u << 16)) | (scalar << 16);
            }
        }

        const std::vector<uint32_t> caps = capabilities(words);
        std::string cap_list;
        for(size_t i = 0; i < caps.size(); ++i){
            if(i) cap_list += ",";
            cap_list += std::to_string(caps[i]);
        }
        out += ",\"capabilities\":[\"" + cap_list + "\"]";

        // The engine only keeps the module bytes when the package has a
        // storage texel buffer binding.
        bool has_texel_buffer = false;
        for(const auto &binding : bindings)
            if(binding.type == VK_DT_STORAGE_TEXEL_BUFFER) has_texel_buffer = true;
        out += ",\"module_words_kept\":" + std::string(has_texel_buffer ? "true" : "false");

        out += ",\"bindings\":[";
        bool first = true;
        for(const auto &binding : bindings){
            if(binding.type != VK_DT_STORAGE_IMAGE && binding.type != VK_DT_STORAGE_TEXEL_BUFFER) continue;
            if(!first) out += ",";
            first = false;
            const uint32_t declared = xbg_spirv_declared_image_format(words, binding.binding);
            XbgStorageImageBinding info;
            const bool has_image = xbg_spirv_binding_image(words, binding.binding, info);
            const ImageUsage usage = has_image
                ? image_usage(words, info.variable) : ImageUsage{};
            const int sample = has_image ? sampled_class(words, info.sampled_type) : -1;

            out += "{\"binding\":" + std::to_string(binding.binding);
            out += ",\"descriptor_type\":" + std::to_string(binding.type);
            out += ",\"count\":" + std::to_string(binding.count);
            out += ",\"flags\":" + std::to_string(binding.flags);
            out += ",\"formatless_write_flag\":" + std::string((binding.flags & 1) ? "true" : "false");
            out += ",\"formatless_read_flag\":" + std::string((binding.flags & 2) ? "true" : "false");
            out += ",\"package_scalar\":" + std::to_string((binding.flags >> 16) & 3);
            out += ",\"module_has_image\":" + std::string(has_image ? "true" : "false");
            out += ",\"image_type_id\":" + std::to_string(has_image ? info.image_type : 0u);
            // How many descriptor variables share this OpTypeImage. The
            // shipping rewrite refuses when the image type is not exclusive.
            {
                uint32_t sharers = 0;
                std::unordered_map<uint32_t, size_t> share_def;
                build_defs(words, share_def);
                for(size_t i = 5; i < words.size();){
                    const uint32_t n = words[i] >> 16, op = words[i] & 0xffffu;
                    if(!n || i + n > words.size()) break;
                    if(op == OP_VARIABLE && n >= 4){
                        const auto ptr = share_def.find(words[i + 1]);
                        if(ptr != share_def.end() && (words[ptr->second] & 0xffffu) == OP_TYPE_POINTER
                           && words[ptr->second + 3] == info.image_type) ++sharers;
                    }
                    i += n;
                }
                out += ",\"image_type_sharers\":" + std::to_string(sharers);
            }
            out += ",\"declared_format\":" + std::to_string(declared);
            out += ",\"sampled_class\":" + std::to_string(sample);
            out += ",\"image_writes\":" + std::to_string(usage.writes);
            out += ",\"image_reads\":" + std::to_string(usage.reads);
            out += ",\"image_fetches\":" + std::to_string(usage.fetches);

            out += ",\"rewrite\":[";
            for(size_t i = 0; i < sizeof(CANDIDATE_FORMATS) / sizeof(CANDIDATE_FORMATS[0]); ++i){
                const uint32_t vk = CANDIDATE_FORMATS[i];
                const uint32_t spirv_format = xbg_spirv_image_format(vk);
                if(i) out += ",";
                std::vector<uint32_t> copy = words;
                const int result = xbg_typed_storage_rewrite(copy, binding.binding, spirv_format);
                const std::string reason = diagnose(words, binding.binding, spirv_format);
                if(result == 1) dump_rewritten(key, binding.binding, vk, copy);
                // The explanation must agree with the shipping verdict.
                const bool agree = (result == 1) == (reason == "accepted");
                if(!agree) ++mismatches;
                out += "{\"vk_format\":" + std::to_string(vk);
                out += ",\"spirv_format\":" + std::to_string(spirv_format);
                out += ",\"result\":" + std::to_string(result);
                out += ",\"reason\":\"" + reason + "\"";
                out += ",\"verdict_agrees\":" + std::string(agree ? "true" : "false");
                out += "}";
            }
            out += "]}";
        }
        out += "]";

        // Module-level variant, exactly as the engine builds it: every
        // formatless-write storage binding whose module format is Unknown is
        // rewritten in binding order, and one refusal abandons the variant.
        {
            std::vector<uint32_t> variant = words;
            bool variant_ok = true;
            std::string variant_reason;
            bool variant_target = false;
            for(const auto &binding : bindings){
                if(binding.type != VK_DT_STORAGE_IMAGE && binding.type != VK_DT_STORAGE_TEXEL_BUFFER) continue;
                if(binding.count != 1) continue;
                if(!(binding.flags & 1)) continue;
                if(xbg_spirv_declared_image_format(words, binding.binding) != XBG_IMAGE_FORMAT_UNKNOWN) continue;
                variant_target = true;
                uint32_t chosen = 0;
                for(size_t i = 0; i < sizeof(CANDIDATE_FORMATS) / sizeof(CANDIDATE_FORMATS[0]); ++i){
                    const uint32_t vk = CANDIDATE_FORMATS[i];
                    std::vector<uint32_t> probe = words;
                    if(xbg_typed_storage_rewrite(probe, binding.binding, xbg_spirv_image_format(vk)) == 1){
                        chosen = vk;
                        break;
                    }
                }
                if(!chosen){ variant_ok = false; variant_reason = "no-accepted-format"; break; }
                if(xbg_typed_storage_rewrite(variant, binding.binding, xbg_spirv_image_format(chosen)) != 1){
                    variant_ok = false;
                    variant_reason = diagnose(words, binding.binding, xbg_spirv_image_format(chosen));
                    break;
                }
            }
            out += ",\"module_variant\":{";
            out += "\"ok\":" + std::string(variant_ok ? "true" : "false");
            out += ",\"reason\":\"" + json_escape(variant_reason) + "\"";
            out += ",\"has_target\":" + std::string(variant_target ? "true" : "false");
            out += ",\"module_words_kept\":" + std::string(has_texel_buffer ? "true" : "false");
            out += "}";
            if(variant_ok && variant_target) dump_module_variant(key, variant);
        }
        out += "}";
        printf("%s\n", out.c_str());
        fflush(stdout);
    }
    fprintf(stderr, "diagnostic mismatches: %zu\n", mismatches);
    return mismatches == 0 ? 0 : 1;
}
