#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

std::mutex mu;
std::atomic<bool> memory_constrained{false};
std::atomic<unsigned> active_workers{0};
std::condition_variable async_cv;
struct {
    int device = 0;
} cfg;
struct Module {
    int module;
};
struct Rec {
    std::vector<uint32_t> words;
};
std::map<int, int> ready, rewritten, layouts, passes;
std::map<int, Module> modules;
std::vector<int> set_layouts, async_q, async_pending;
std::vector<Rec> recs;
size_t rewritten_bytes = 0;
unsigned destroyed_pipelines = 0, destroyed_modules = 0, destroyed_layouts = 0;
void vkDestroyPipeline(int, int, void *) {
    ++destroyed_pipelines;
}
void vkDestroyShaderModule(int, int, void *) {
    ++destroyed_modules;
}
void vkDestroyPipelineLayout(int, int, void *) {
    ++destroyed_layouts;
}
void vkDestroyDescriptorSetLayout(int, int, void *) {
    ++destroyed_layouts;
}
void vkDestroyRenderPass(int, int, void *) {
    ++destroyed_layouts;
}

#include PRODUCTION_HEADER

void initialize() {
    memory_constrained = false;
    destroyed_pipelines = destroyed_modules = destroyed_layouts = 0;
    ready = {{1, 10}, {2, 20}};
    rewritten = {{1, 2}};
    rewritten_bytes = 1024;
    layouts = {{1, 2}};
    passes = {{1, 2}};
    modules = {{1, {2}}};
    set_layouts = {1};
    recs = {{{1, 2, 3}}};
    async_q = async_pending = {1};
}

int main() {
    for (unsigned workers : {0u, 1u, 3u}) {
        for (bool idle : {false, true}) {
            initialize();
            active_workers = workers;
            assert(trim_for_memory_pressure(idle) == 2);
            assert(memory_constrained && destroyed_pipelines == 2 && ready.empty());
            assert(rewritten.empty() && !rewritten_bytes && async_q.empty() &&
                   async_pending.empty());
            if (workers || !idle) {
                if (destroyed_modules || destroyed_layouts || recs.empty())
                    return 12;
                assert(modules.size() == 1 && layouts.size() == 1 && set_layouts.size() == 1);
            } else {
                assert(destroyed_modules == 1 && destroyed_layouts == 3);
                assert(modules.empty() && layouts.empty() && passes.empty() && set_layouts.empty());
                assert(recs.empty());
            }
            assert(trim_for_memory_pressure(idle) == 0);
        }
    }
    initialize();
    active_workers = 0;
    std::atomic<unsigned> phase{0};
    std::thread worker([&] {
        WorkerLifetime lifetime;
        phase.store(1);
        while (phase.load() != 2)
            std::this_thread::yield();
        assert(modules.size() == 1);
    });
    while (phase.load() != 1)
        std::this_thread::yield();
    trim_for_memory_pressure(true);
    assert(modules.size() == 1 && active_workers.load() == 1);
    phase.store(2);
    worker.join();
    trim_for_memory_pressure(true);
    assert(modules.empty() && active_workers.load() == 0);
}
