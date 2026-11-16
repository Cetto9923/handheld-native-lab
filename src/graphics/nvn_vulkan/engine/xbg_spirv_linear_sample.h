#pragma once
#include "xbg_spirv_sample_usage.h"
#include <algorithm>
#include <cstring>

namespace xbg_linear_sample_detail {
struct Sample {
    size_t offset;
    uint32_t binding, image_type, float_type, coordinate_type, result_type;
};
struct Module {
    std::vector<size_t> instructions;
    std::map<uint32_t, uint32_t> binding, descriptor_set, loaded_variable, value_type,
        sampled_image;
    std::map<uint32_t, std::vector<uint32_t>> image_types;
    std::set<uint32_t> floats, zero;
    std::vector<Sample> samples;
};
inline bool inspect(const std::vector<uint32_t> &w, Module &m) {
    if (w.size() < 5 || w[0] != 0x07230203 || !w[3])
        return false;
    for (size_t i = 5; i < w.size();) {
        uint32_t n = w[i] >> 16, op = w[i] & 65535;
        if (!n || n > w.size() - i)
            return false;
        m.instructions.push_back(i);
        if (op == 71 && n == 4) {
            if (w[i + 2] == 33)
                m.binding[w[i + 1]] = w[i + 3];
            if (w[i + 2] == 34)
                m.descriptor_set[w[i + 1]] = w[i + 3];
        }
        if (op == 22 && n == 3 && w[i + 2] == 32)
            m.floats.insert(w[i + 1]);
        if (op == 25 && n >= 9)
            m.image_types[w[i + 1]] = {w.begin() + i + 2, w.begin() + i + n};
        if (op == 27 && n == 3)
            m.sampled_image[w[i + 1]] = w[i + 2];
        if (op == 43 && n == 4 && !w[i + 3])
            m.zero.insert(w[i + 2]);
        if (op == 61 && n >= 4) {
            m.loaded_variable[w[i + 2]] = w[i + 3];
            m.value_type[w[i + 2]] = w[i + 1];
        }
        if ((op == 80 || op == 79 || op == 169) && n >= 4)
            m.value_type[w[i + 2]] = w[i + 1];
        if (op == 83 && n == 4) {
            auto source = m.loaded_variable.find(w[i + 3]);
            if (source != m.loaded_variable.end())
                m.loaded_variable[w[i + 2]] = source->second;
            m.value_type[w[i + 2]] = w[i + 1];
        }
        i += n;
    }
    for (size_t i : m.instructions) {
        if ((w[i] & 65535) != 88 || (w[i] >> 16) != 7 || w[i + 5] != 2 || !m.zero.count(w[i + 6]))
            continue;
        auto loaded = m.loaded_variable.find(w[i + 3]), type = m.value_type.find(w[i + 3]);
        if (loaded == m.loaded_variable.end() || type == m.value_type.end())
            continue;
        auto binding = m.binding.find(loaded->second), set = m.descriptor_set.find(loaded->second);
        auto sampled = m.sampled_image.find(type->second);
        if (binding == m.binding.end() || set == m.descriptor_set.end() || set->second ||
            sampled == m.sampled_image.end())
            continue;
        auto image = m.image_types.find(sampled->second);
        if (image == m.image_types.end())
            continue;
        const auto &t = image->second;
        // float32, 2D, non-depth, non-array, non-MS, sampled image.
        if (t.size() < 7 || !m.floats.count(t[0]) || t[1] != 1 || t[2] != 0 || t[3] != 0 ||
            t[4] != 0 || t[5] != 1)
            continue;
        // The original coordinate/result types are checked against OpTypeVector.
        auto coordinate_value = m.value_type.find(w[i + 4]);
        if (coordinate_value == m.value_type.end())
            continue;
        uint32_t coordinate = 0;
        bool result = false;
        for (size_t j : m.instructions) {
            if ((w[j] & 65535) == 23 && (w[j] >> 16) == 4 && w[j + 2] == t[0]) {
                if (w[j + 3] == 2 && w[j + 1] == coordinate_value->second)
                    coordinate = w[j + 1];
                if (w[j + 3] == 4 && w[j + 1] == w[i + 1])
                    result = true;
            }
        }
        if (!coordinate || !result)
            continue;
        m.samples.push_back({i, binding->second, sampled->second, t[0], coordinate, w[i + 1]});
    }
    return true;
}
inline void emit(std::vector<uint32_t> &out, uint32_t op, std::initializer_list<uint32_t> args) {
    out.push_back(uint32_t(args.size() + 1) << 16 | op);
    out.insert(out.end(), args.begin(), args.end());
}
} // namespace xbg_linear_sample_detail

// Only explicit, unoffset LOD zero samples are rewritten. Each selected binding
// must become wholly independent of filtering after the transaction.
inline bool xbg_spirv_linear_sample(std::vector<uint32_t> &words,
                                    const std::vector<uint32_t> &bindings) {
    using namespace xbg_linear_sample_detail;
    if (bindings.empty())
        return true;
    Module module;
    if (!inspect(words, module))
        return false;
    std::map<size_t, Sample> selected;
    std::set<uint32_t> covered;
    for (const auto &sample : module.samples)
        if (std::find(bindings.begin(), bindings.end(), sample.binding) != bindings.end()) {
            selected.emplace(sample.offset, sample);
            covered.insert(sample.binding);
        }
    for (uint32_t binding : bindings)
        if (!covered.count(binding))
            return false;
    uint32_t next = words[3];
    if (next > UINT32_MAX - 16 || selected.size() > (UINT32_MAX - next - 16) / 64)
        return false;
    std::vector<uint32_t> declarations, functions, capabilities;
    uint32_t glsl = 0;
    bool image_query = false;
    for (size_t i : module.instructions) {
        if ((words[i] & 65535) == 17 && (words[i] >> 16) == 2 && words[i + 1] == 50)
            image_query = true;
        if ((words[i] & 65535) == 11 && (words[i] >> 16) >= 6 &&
            !std::memcmp(&words[i + 2], "GLSL.std.450\0", 13))
            glsl = words[i + 1];
    }
    // The helper uses core arithmetic and Floor. Reuse the game's import when present.
    std::vector<uint32_t> imports;
    if (!glsl) {
        glsl = next++;
        imports = {6u << 16 | 11, glsl, 0x4c534c47, 0x6474732e, 0x3035342e, 0};
    }
    if (!image_query)
        emit(capabilities, 17, {50});
    uint32_t int_type = 0, int2 = 0;
    for (size_t i : module.instructions)
        if ((words[i] & 65535) == 21 && (words[i] >> 16) == 4 && words[i + 2] == 32 &&
            words[i + 3] == 1)
            int_type = words[i + 1];
    if (!int_type) {
        int_type = next++;
        emit(declarations, 21, {int_type, 32, 1});
    }
    for (size_t i : module.instructions)
        if ((words[i] & 65535) == 23 && (words[i] >> 16) == 4 && words[i + 2] == int_type &&
            words[i + 3] == 2)
            int2 = words[i + 1];
    if (!int2) {
        int2 = next++;
        emit(declarations, 23, {int2, int_type, 2});
    }
    uint32_t component[4];
    for (unsigned c = 0; c < 4; ++c) {
        component[c] = next++;
        emit(declarations, 43, {int_type, component[c], c});
    }
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> float_constants;
    for (const auto &entry : selected) {
        const Sample &s = entry.second;
        if (float_constants.count(s.float_type))
            continue;
        uint32_t one = next++, half = next++, half2 = next++;
        emit(declarations, 43, {s.float_type, one, 0x3f800000});
        emit(declarations, 43, {s.float_type, half, 0x3f000000});
        emit(declarations, 44, {s.coordinate_type, half2, half, half});
        float_constants[s.float_type] = {one, half2};
    }
    for (size_t i : module.instructions) {
        auto found = selected.find(i);
        if (found == selected.end()) {
            functions.insert(functions.end(), words.begin() + i,
                             words.begin() + i + (words[i] >> 16));
            continue;
        }
        const Sample &s = found->second;
        const uint32_t image = next++, size = next++, sizef = next++, scaled = next++,
                       center = next++, floor = next++, fraction = next++;
        const uint32_t sampled = words[i + 3], uv = words[i + 4];
        const auto constants = float_constants.at(s.float_type);
        emit(functions, 100, {s.image_type, image, sampled});
        emit(functions, 103, {int2, size, image, component[0]});
        emit(functions, 111, {s.coordinate_type, sizef, size});
        emit(functions, 133, {s.coordinate_type, scaled, uv, sizef});
        emit(functions, 131, {s.coordinate_type, center, scaled, constants.second});
        emit(functions, 12, {s.coordinate_type, floor, glsl, 8, center});
        emit(functions, 131, {s.coordinate_type, fraction, center, floor});
        uint32_t x = next++, y = next++, ix = next++, iy = next++;
        emit(functions, 81, {s.float_type, x, fraction, 0});
        emit(functions, 81, {s.float_type, y, fraction, 1});
        emit(functions, 131, {s.float_type, ix, constants.first, x});
        emit(functions, 131, {s.float_type, iy, constants.first, y});
        // Gather order is (i0,j1), (i1,j1), (i1,j0), (i0,j0).
        uint32_t weight[4];
        const uint32_t left[] = {ix, x, x, ix}, right[] = {y, y, iy, iy};
        for (unsigned c = 0; c < 4; ++c) {
            weight[c] = next++;
            emit(functions, 133, {s.float_type, weight[c], left[c], right[c]});
        }
        uint32_t weights = next++;
        emit(functions, 80, {s.result_type, weights, weight[0], weight[1], weight[2], weight[3]});
        uint32_t result[4];
        for (unsigned c = 0; c < 4; ++c) {
            uint32_t gather = next++;
            result[c] = next++;
            emit(functions, 96, {s.result_type, gather, sampled, uv, component[c]});
            emit(functions, 148, {s.float_type, result[c], gather, weights});
        }
        emit(functions, 80,
             {s.result_type, words[i + 2], result[0], result[1], result[2], result[3]});
    }
    std::vector<uint32_t> out(words.begin(), words.begin() + 5);
    out[3] = next;
    size_t at = 0;
    // Capabilities precede extensions/imports; imports precede the memory model.
    while (at < functions.size() && (functions[at] & 65535) == 17) {
        size_t n = functions[at] >> 16;
        out.insert(out.end(), functions.begin() + at, functions.begin() + at + n);
        at += n;
    }
    out.insert(out.end(), capabilities.begin(), capabilities.end());
    bool imported = imports.empty(), declared = false;
    for (size_t i = at; i < functions.size();) {
        uint32_t n = functions[i] >> 16, op = functions[i] & 65535;
        if (!imported && op == 14) {
            out.insert(out.end(), imports.begin(), imports.end());
            imported = true;
        }
        if (!declared && op == 54) {
            out.insert(out.end(), declarations.begin(), declarations.end());
            declared = true;
        }
        out.insert(out.end(), functions.begin() + i, functions.begin() + i + n);
        i += n;
    }
    if (!imported || !declared)
        return false;
    const auto independent = xbg_spirv_filter_independent(out);
    for (uint32_t binding : bindings)
        if (!independent.count(binding))
            return false;
    words.swap(out);
    return true;
}

inline std::vector<uint32_t>
xbg_spirv_linear_sample_candidates(const std::vector<uint32_t> &words) {
    xbg_linear_sample_detail::Module module;
    if (!xbg_linear_sample_detail::inspect(words, module))
        return {};
    std::set<uint32_t> possible;
    for (const auto &sample : module.samples)
        possible.insert(sample.binding);
    std::vector<uint32_t> result;
    for (uint32_t binding : possible) {
        auto changed = words;
        if (xbg_spirv_linear_sample(changed, {binding}))
            result.push_back(binding);
    }
    return result;
}
