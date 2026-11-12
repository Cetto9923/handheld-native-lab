// Contract for the learned pipeline list: the schedule it is written on, the
// pending bytes that reach the disk, and the wiring in the engine.
//
// Self-authored bytes only. This checks the bookkeeping, not Vulkan pipeline
// validity, driver support or performance.
#include "xbg_warm_flush.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &message) {
    if (ok) return;
    ++failures;
    std::cerr << "FAIL " << message << "\n";
}

std::string read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string temp_path(const char *name) {
    const char *dir = std::getenv("TMPDIR");
    std::string base = dir ? dir : "/tmp";
    if (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/xbg-warm-flush-" + name;
}

// The schedule of the learned list depends on the presented frames and on
// nothing else. A session that only built pipelines the driver had cached
// still writes its recipes.
void test_schedule() {
    check(!xbg_warm_flush::due(0, 0), "a fresh session is not due");
    check(!xbg_warm_flush::due(299, 0), "the list is not due before the interval");
    check(xbg_warm_flush::due(300, 0), "the list is due at the interval");
    check(xbg_warm_flush::due(301, 0), "the list stays due after the interval");
    check(!xbg_warm_flush::due(599, 300), "the list is not due one frame early");
    check(xbg_warm_flush::due(600, 300), "the list is due one interval later");
    check(xbg_warm_flush::due(0, 0) == false && xbg_warm_flush::due(300, 0) == true,
          "the schedule is decided by the frames alone");
}

// The pending recipes: exact bytes, no loss when the write fails, and no
// loss or duplication when a flush runs while recipes are still recorded.
void test_pending_bytes() {
    xbg_warm_flush::Pending pending;
    std::vector<uint8_t> out;
    check(!pending.take(out), "an empty list has nothing to write");
    check(pending.empty(), "an empty list stays empty");

    const uint8_t first[] = {1, 2, 3, 4};
    const uint8_t second[] = {5, 6};
    pending.append(first, sizeof(first));
    pending.append(second, sizeof(second));
    check(pending.size() == sizeof(first) + sizeof(second), "the pending size counts the bytes");

    check(pending.take(out), "the pending recipes are taken");
    check(out.size() == sizeof(first) + sizeof(second), "the taken bytes keep their size");
    check(std::equal(out.begin(), out.begin() + sizeof(first), first), "the first recipe keeps its bytes");
    check(std::equal(out.begin() + sizeof(first), out.end(), second), "the second recipe keeps its bytes");
    check(pending.empty(), "a taken list is empty again");

    // A failed write hands the recipes back: nothing is lost.
    pending.restore(out);
    check(pending.size() == out.size(), "a failed write keeps the recipes");
    std::vector<uint8_t> again;
    check(pending.take(again), "the kept recipes are written by the next attempt");
    check(again == out, "the kept recipes are the same bytes");
}

// Concurrent recording during a flush: no recorded byte is lost and none is
// written twice. The flush cycles are bounded, so the check cannot livelock.
void test_concurrent_record_and_flush() {
    xbg_warm_flush::Pending pending;
    const int writers = 4;
    const int per_writer = 250;
    const size_t chunk = 16;
    const size_t expected_blocks = size_t(writers) * per_writer;
    std::atomic<bool> stop{false};
    std::atomic<size_t> cycles{0};

    // The flush takes the pending recipes and gives them back: the write of
    // this test is a no-op, so the bytes must be there at the end.
    std::thread flusher([&]() {
        std::vector<uint8_t> out;
        while (!stop.load() && cycles.fetch_add(1) < 200) {
            if (pending.take(out)) {
                pending.restore(out);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::vector<std::thread> threads;
    for (int w = 0; w < writers; ++w) {
        threads.emplace_back([&, w]() {
            for (int i = 0; i < per_writer; ++i) {
                // Every block is unique: the writer and the index are in it.
                uint8_t block[16];
                for (size_t k = 0; k < chunk; ++k) block[k] = 0;
                block[0] = uint8_t(w);
                block[1] = uint8_t(w >> 8);
                block[2] = uint8_t(i);
                block[3] = uint8_t(i >> 8);
                pending.append(block, chunk);
            }
        });
    }
    for (auto &t : threads) t.join();
    stop.store(true);
    flusher.join();

    std::vector<uint8_t> out;
    check(pending.take(out), "the concurrent recipes are pending at the end");
    check(out.size() == expected_blocks * chunk,
          "every recorded byte is still pending after concurrent flushes");
    // Every block is present exactly once.
    std::map<std::string, int> seen;
    for (size_t at = 0; at + chunk <= out.size(); at += chunk) {
        seen[std::string(reinterpret_cast<const char *>(&out[at]), chunk)]++;
    }
    check(seen.size() == expected_blocks, "no recorded recipe is lost or duplicated");
    bool single = true;
    for (const auto &entry : seen) single = single && entry.second == 1;
    check(single, "every recorded recipe appears exactly once");
}

// The file write: exact content, appended, and a refused path keeps the data.
void test_append_file() {
    const std::string path = temp_path("list.bin");
    std::remove(path.c_str());
    const std::vector<uint8_t> first = {9, 8, 7};
    const std::vector<uint8_t> second = {6};
    check(xbg_warm_flush::append_file(path, first), "the list is written");
    check(xbg_warm_flush::append_file(path, second), "the list is appended");
    const std::string content = read_file(path);
    check(content.size() == first.size() + second.size(), "the file holds both writes");
    check(content.compare(0, first.size(), std::string(first.begin(), first.end())) == 0,
          "the file holds the first bytes");
    check(content.compare(first.size(), second.size(), std::string(second.begin(), second.end())) == 0,
          "the file holds the appended bytes");

    const std::string refused = temp_path("no-such-directory/list.bin");
    check(!xbg_warm_flush::append_file(refused, first), "an unwritable path is refused");
    check(!xbg_warm_flush::append_file(std::string(), first), "an empty path is refused");
    std::remove(path.c_str());
}

// The engine wiring: the learned list is flushed before the pipeline-cache
// guard, and on its own schedule in the frame loop. This is the defect the
// contract is about: the flush used to sit behind "no pipeline took 2 ms".
void test_engine_wiring(const std::string &root) {
    const std::string source = read_file(root + "/src/graphics/nvn_vulkan/engine/xbg_game.cpp");
    check(!source.empty(), "the engine source is readable");
    if (source.empty()) return;

    const size_t save = source.find("void save_pipeline_cache(){");
    check(save != std::string::npos, "save_pipeline_cache is present");
    if (save == std::string::npos) return;
    const size_t flush = source.find("warm::flush();", save);
    const size_t guard = source.find("if(!pipeline_cache || !pipelines_unsaved", save);
    check(flush != std::string::npos, "save_pipeline_cache flushes the learned list");
    check(guard != std::string::npos, "the pipeline-cache guard is present");
    check(flush != std::string::npos && guard != std::string::npos && flush < guard,
          "the learned list is flushed before the pipeline-cache guard");

    check(source.find("xbg_warm_flush::due(presents,warm_save_frame)") != std::string::npos,
          "the frame loop writes the learned list on its own schedule");
    check(source.find("warm_save_frame=presents") != std::string::npos,
          "the learned list advances its own schedule");
}

}  // namespace

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    test_schedule();
    test_pending_bytes();
    test_concurrent_record_and_flush();
    test_append_file();
    test_engine_wiring(root);
    if (failures) {
        std::cerr << failures << " warm flush contract check(s) failed\n";
        return 1;
    }
    std::cout << "warm flush contract: all checks passed\n";
    return 0;
}
