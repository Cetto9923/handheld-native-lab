#include "xbg_spirv_inline.h"
#include <spirv-tools/optimizer.hpp>
#include <unordered_set>

// The scan follows the upstream SPIR-V operation/function boundaries. Invalid
// instructions are rejected rather than silently sent to the driver.
static int helper_subgroups(const std::vector<uint32_t> &words) {
    if (words.size() < 5 || words[0] != 0x07230203u) return -1;
    std::unordered_set<uint32_t> entry;
    uint32_t current = 0;
    bool inside = false, found = false;
    for (size_t i = 5; i < words.size();) {
        const uint32_t op = words[i] & 0xffffu, count = words[i] >> 16;
        if (!count || count > words.size() - i) return -1;
        if (op == 15 && count > 2) entry.insert(words[i + 2]);
        else if (op == 54 && count > 2) { current = words[i + 2]; inside = true; }
        else if (op == 56) inside = false;
        else if (op >= 333 && op <= 366 && inside && !entry.count(current)) found = true;
        i += count;
    }
    return found ? 1 : 0;
}

XbgInlineResult xbg_inline_subgroup_helpers(std::vector<uint32_t> &words) {
    const int has = helper_subgroups(words);
    if (has < 0) return XbgInlineResult::Invalid;
    if (!has) return XbgInlineResult::Unchanged;
    spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_1);
    optimizer.RegisterPass(spvtools::CreateMergeReturnPass());
    optimizer.RegisterPass(spvtools::CreateInlineExhaustivePass());
    optimizer.RegisterPass(spvtools::CreateEliminateDeadFunctionsPass());
    std::vector<uint32_t> rewritten;
    if (!optimizer.Run(words.data(), words.size(), &rewritten) || helper_subgroups(rewritten) != 0)
        return XbgInlineResult::Invalid;
    words.swap(rewritten);
    return XbgInlineResult::Changed;
}
