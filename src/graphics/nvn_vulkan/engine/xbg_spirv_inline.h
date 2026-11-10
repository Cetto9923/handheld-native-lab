#pragma once
#include <cstdint>
#include <vector>

// Adapted from d2r-mobile (MIT, copyright 2026 jhaizhou), revision
// f1e4b081af8ea65d95f0bf490a568cb657c3917e. SPIRV-Tools is Apache-2.0.
enum class XbgInlineResult { Unchanged, Changed, Invalid };
XbgInlineResult xbg_inline_subgroup_helpers(std::vector<uint32_t> &words);
