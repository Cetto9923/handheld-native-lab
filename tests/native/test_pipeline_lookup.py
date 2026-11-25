"""Exercise production pipeline_lookup negative caching and cold LRU eviction."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'

STUB = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using VkDevice = int;
using VkPipeline = uint64_t;
using VkPipelineCache = int;
using VkShaderModule = uint64_t;
using VkResult = int;
constexpr VkPipeline VK_NULL_HANDLE = 0;
constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_ERROR_OUT_OF_HOST_MEMORY = -1;
constexpr VkResult VK_ERROR_OUT_OF_DEVICE_MEMORY = -2;
constexpr VkResult VK_ERROR_DEVICE_LOST = -4;
constexpr VkResult VK_ERROR_UNKNOWN = -13;
constexpr uint32_t VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR = 1;

struct VkPipelineShaderStageCreateInfo { VkShaderModule module = 0; };
struct VkGraphicsPipelineCreateInfo {
    uint32_t flags = 0;
    uint32_t stageCount = 0;
    const VkPipelineShaderStageCreateInfo *pStages = nullptr;
};
struct VkComputePipelineCreateInfo {
    uint32_t flags = 0;
    VkPipelineShaderStageCreateInfo stage{};
};
struct XbgPipelineKey {
    uint32_t words[16]{};
    uint32_t count = 1;
    bool valid = true;
};

struct PipelineEntry { VkPipeline pipeline{}; uint64_t last_used = 0; };
struct PipelineCounts { uint64_t hits = 0, misses = 0, evictions = 0, bypass = 0; };
struct PipelineLease { VkPipeline pipeline{}; bool transient = false; };
struct Shader { std::string key; };
struct { Shader *shader[6]{}; } state;
struct { VkDevice device = 1; } cfg;
struct { std::atomic<uint64_t> presents{1}; } stats;
namespace hud { std::atomic<uint64_t> pipelines{0}, pipeline_ns{0}; }
struct VecHash {
    size_t operator()(const std::vector<uint32_t> &v) const {
        size_t h = v.size();
        for (uint32_t x : v) h ^= x + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};
enum XbgApiKind { RuntimePipeline };
struct {
    uint64_t begin(const char *, int, uint64_t) { return 1; }
    void end(uint64_t, int, uint64_t, int) {}
} api_breadcrumb;
namespace XbgApiBreadcrumb { constexpr int RuntimePipeline = 1; }

VkPipelineCache pipeline_cache = 1;
std::unordered_map<std::string, PipelineEntry> pipeline_objects;
std::unordered_map<std::vector<uint32_t>, VkPipeline, VecHash> pipeline_fast;
PipelineCounts pipeline_counts{};
uint64_t pipeline_tick = 0;
constexpr size_t pipeline_object_limit = 4;
bool honor_pipeline_diagnostics = false, pipe_stats = false;
uint64_t compile_ns = 10000000, pipelines_unsaved = 0;
std::vector<VkPipeline> retired_destroyed;
uint64_t pipe_memo_clears = 0;

void xbg_logf(const char *, ...) {}
std::string pstat_what() { return "test"; }
void pstat_log(VkPipeline) {}
uint64_t timing_clock() { static uint64_t t = 0; return t += 1000; }
void retire_handle(std::function<void()> fn) { fn(); }
void vkDestroyPipeline(VkDevice, VkPipeline p, void *) { retired_destroyed.push_back(p); }
void clear_pipe_memo() { ++pipe_memo_clears; }

namespace warm {
uint64_t hits = 0, creates = 0, create_ns = 0, create_max_ns = 0;
uint64_t compute_hits = 0, compute_creates = 0, compute_create_ns = 0, compute_create_max_ns = 0;
uint64_t async_skips = 0;
bool take(const std::string &, VkPipeline &) { return false; }
bool async_request(const VkGraphicsPipelineCreateInfo &, const XbgPipelineKey &, const std::string &) { return false; }
void record_failure(const VkPipelineShaderStageCreateInfo *, uint32_t, const XbgPipelineKey &, VkResult, uint64_t) {}
void record(const VkGraphicsPipelineCreateInfo &, const XbgPipelineKey &) {}
void record(const VkComputePipelineCreateInfo &, const XbgPipelineKey &) {}
std::string list_path() { return "/tmp/x"; }
void flush() {}
} // namespace warm

VkResult next_gfx_result = VK_SUCCESS;
VkResult next_cmp_result = VK_SUCCESS;
uint64_t next_pipeline_id = 100;
unsigned gfx_create_calls = 0, cmp_create_calls = 0;

static VkResult create_graphics_pipeline_resilient(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const void *, VkPipeline *out) {
    ++gfx_create_calls;
    if (next_gfx_result != VK_SUCCESS) { *out = VK_NULL_HANDLE; return next_gfx_result; }
    *out = ++next_pipeline_id;
    return VK_SUCCESS;
}
static VkResult create_compute_pipeline_resilient(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo *, const void *, VkPipeline *out) {
    ++cmp_create_calls;
    if (next_cmp_result != VK_SUCCESS) { *out = VK_NULL_HANDLE; return next_cmp_result; }
    *out = ++next_pipeline_id;
    return VK_SUCCESS;
}
'''

CHECK = r'''
static XbgPipelineKey make_key(uint32_t id) {
    XbgPipelineKey k;
    k.words[0] = id;
    k.count = 1;
    k.valid = true;
    return k;
}

int main() {
    VkGraphicsPipelineCreateInfo gci{};
    VkComputePipelineCreateInfo cci{};

    // 1. Transient errors (-1, -2, -4) are never negative-cached and recover next frame.
    for (VkResult err : {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST}) {
        next_gfx_result = err;
        PipelineLease lease;
        pipeline_lookup(lease, make_key(1), true, &gci, nullptr);
        assert(lease.pipeline == VK_NULL_HANDLE && lease.transient);
        assert(pipeline_objects.empty());
    }
    next_gfx_result = VK_SUCCESS;
    {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(1), true, &gci, nullptr);
        assert(lease.pipeline != VK_NULL_HANDLE && !lease.transient);
        assert(pipeline_objects.size() == 1);
    }

    // Same for compute transient vs VK_ERROR_UNKNOWN.
    next_cmp_result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(2), true, nullptr, &cci);
        assert(lease.pipeline == VK_NULL_HANDLE && lease.transient);
        assert(pipeline_objects.size() == 1);
    }

    // 2. VK_ERROR_UNKNOWN (-13) is cached with last_used=0 and stays cold on repeated hits.
    next_gfx_result = VK_ERROR_UNKNOWN;
    unsigned calls_before = gfx_create_calls;
    {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(10), true, &gci, nullptr);
        assert(lease.pipeline == VK_NULL_HANDLE && !lease.transient);
        assert(gfx_create_calls == calls_before + 1);
        assert(pipeline_objects.size() == 2);
    }
    for (int i = 0; i < 5; ++i) {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(10), true, &gci, nullptr);
        assert(lease.pipeline == VK_NULL_HANDLE);
        assert(gfx_create_calls == calls_before + 1);
    }

    // Fill remaining 2 slots with live pipelines (limit == 4).
    next_gfx_result = VK_SUCCESS;
    for (uint32_t k : {3u, 4u}) {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(k), true, &gci, nullptr);
        assert(lease.pipeline != VK_NULL_HANDLE);
    }
    assert(pipeline_objects.size() == 4);

    // 3. New live pipeline evicts the cold VK_NULL_HANDLE entry first, preserving all 3 live ones
    // and never calling vkDestroyPipeline(0) or clear_pipe_memo().
    retired_destroyed.clear();
    pipe_memo_clears = 0;
    {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(5), true, &gci, nullptr);
        assert(lease.pipeline != VK_NULL_HANDLE);
        assert(pipeline_objects.size() == 4);
        assert(retired_destroyed.empty() && pipe_memo_clears == 0);
        // Key 10 was evicted; verify all 4 entries now hold live pipelines.
        for (const auto &kv : pipeline_objects) assert(kv.second.pipeline != VK_NULL_HANDLE && kv.second.last_used > 0);
    }

    // 4. With all 4 slots holding live pipelines, a new VK_ERROR_UNKNOWN must NOT evict a live pipeline.
    next_cmp_result = VK_ERROR_UNKNOWN;
    retired_destroyed.clear();
    {
        PipelineLease lease;
        pipeline_lookup(lease, make_key(99), true, nullptr, &cci);
        assert(lease.pipeline == VK_NULL_HANDLE && lease.transient);
        assert(pipeline_objects.size() == 4 && retired_destroyed.empty());
        for (const auto &kv : pipeline_objects) assert(kv.second.pipeline != VK_NULL_HANDLE);
    }
    return 0;
}
'''


class PipelineLookupTest(unittest.TestCase):
    def test_negative_cache_and_cold_eviction(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        start = source.index('void pipeline_lookup(')
        end = source.index('void clear_pipeline_objects(', start)
        code = STUB + source[start:end] + CHECK
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            cpp = p / 'test.cpp'
            exe = p / 'test'
            cpp.write_text(code)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
