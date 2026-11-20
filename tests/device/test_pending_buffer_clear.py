"""Run OP_CLEAR_BUF with a guest buffer and a GPU allocation still in flight."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / 'src/graphics/nvn_vulkan/engine'


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


PREAMBLE = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
#define CPU_SCOPE(...)
#define xbg_logf(...)
struct Slot {};
struct Buffer {
    void *mapped;
    void invalidate() {}
};
struct Writeback {
    uint64_t addr, bytes;
    Buffer buffer;
    bool indirect;
    const void *owner = nullptr;
    const void *slot = nullptr;
};
void retire_honor_clear_producer(uint64_t, uint64_t) {}
bool clear_sync_census = false;
unsigned clear_sync_records = 0;
struct NVNcounterData { uint64_t value, timestamp; };
struct Report { uint64_t addr; };
struct p_clear_buf { uint64_t addr, size; uint32_t value; };
struct Image {
    struct { uint8_t *mem; struct {size_t size;} L; } source;
    bool gpu_written, rg16_proxy, uploaded;
    uint64_t checked_submission;
} image;
bool has_image, draw_detail, honor_pipeline_diagnostics, resolving_pending;
constexpr bool log_write_sync = false;
unsigned cur_op = 0;
struct {void image() {}} draw_timing;
struct {int why_access, access_by[12], write_sync_by_op[64];} perf;
std::vector<Writeback> pending_writebacks;
std::vector<Report> pending_reports;
int sync_count;
uint64_t invalid_address, invalid_bytes;
void snapshot_invalidate(uint64_t address, uint64_t bytes) {
    invalid_address = address;
    invalid_bytes = bytes;
}
namespace evguard {void unguard(uint64_t) {}}
void need(bool ok, const char *) {assert(ok);}
void provenance_write(Image &, int) {}
void probe_cpu_sync(uint64_t, uint64_t, unsigned, uint64_t, uint64_t) {}
template<class Visitor>
void for_images_overlapping(uint64_t address, size_t bytes, Visitor visit) {
    if (has_image && xbg_wb_overlaps(address, bytes, uintptr_t(image.source.mem), image.source.L.size))
        visit(image);
}
void resolve_pending() {
    ++sync_count;
    for (const auto &w : pending_writebacks)
        memcpy(reinterpret_cast<void *>(w.addr), w.buffer.mapped, w.bytes);
    for (const auto &r : pending_reports)
        memset(reinterpret_cast<void *>(r.addr), 0x77, sizeof(NVNcounterData));
    pending_reports.clear();
    pending_writebacks.clear();
}
void trace_buffer_clear(const p_clear_buf &) {}
bool gpu_ordered_buffer_clear(const p_clear_buf &) { return false; }
bool gpu_clear_pending_buffer(const p_clear_buf &) { return false; }
enum {OP_CLEAR_BUF};
template<class T> T payload(const void *p, size_t n) {
    assert(n == sizeof(T));
    return *static_cast<const T *>(p);
}
'''

CHECK = r'''
int main() {
    for (int example = 0; example < 6; ++example) {
        alignas(8) std::array<uint8_t, 32> guest, gpu;
        guest.fill(0x11);
        gpu.fill(0x66);
        const auto old_gpu = gpu;
        auto expected = guest;
        uint64_t base = uintptr_t(guest.data());
        const bool partial = example == 1;
        const bool unrelated = example == 2;
        const bool counter = example == 3;
        const bool alias = example == 4;
        const unsigned offset = partial ? 4 : example == 5 ? 1 : 0;
        const unsigned bytes = example == 5 ? 7 : 8;
        const uint32_t pattern = 0x7a419e28;
        sync_count = 0;
        has_image = alias;
        image = {{guest.data(), {8}}, true, false, true, 0};
        pending_reports.clear();
        pending_writebacks.clear();
        if (counter) {
            pending_reports.push_back({base});
            memset(expected.data(), 0x77, sizeof(NVNcounterData));
        } else if (example != 5) {
            pending_writebacks.push_back({base + (unrelated ? 16u : 0u),
                                          partial ? 16u : 8u, {gpu.data()}, true});
            if (partial) memcpy(expected.data(), gpu.data(), 16);
        }
        for (unsigned i = 0; i < bytes; ++i)
            expected[offset+i] = reinterpret_cast<const uint8_t *>(&pattern)[i % 4];
        p_clear_buf command{base + offset, bytes, pattern};
        execute(&command, sizeof(command));
        // GPU commands already recorded may still read this allocation.
        if (gpu != old_gpu) return 9;
        if (guest != expected || sync_count != int(partial || counter)) return 10;
        if (invalid_address != command.addr || invalid_bytes != bytes) return 11;
        // A pending indirect proof must disappear when its guest bytes change.
        if (pending_writebacks.size() != size_t(unrelated)) return 12;
        if (alias && (image.gpu_written || image.uploaded || image.checked_submission != UINT64_MAX))
            return 13;
    }
}
'''


def program(source, preamble=PREAMBLE):
    first = source.index('case OP_CLEAR_BUF:')
    case = source[first:source.index('case OP_DEPTH_CLAMP:', first)]
    return '\n'.join((preamble,
                      function(source, 'void cpu_access(uint64_t address,uint64_t bytes,unsigned why){'),
                      function(source, 'void invalidate_cpu_range(uint64_t address,uint64_t bytes){'),
                      'void execute(const void *p, size_t n) { switch (OP_CLEAR_BUF) {',
                      case, '}}', CHECK))


def run_program(code):
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        cpp = path / 'clear.cpp'
        cpp.write_text(code)
        binary = path / 'clear'
        subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                        '-I', str(ENGINE), str(cpp), '-o', str(binary)], check=True)
        return subprocess.run([str(binary)], check=False).returncode


class PendingBufferClear(unittest.TestCase):
    def test_guest_clear_preserves_outstanding_gpu_reads(self):
        source = (ENGINE / 'xbg_game.cpp').read_text()
        self.assertEqual(run_program(program(source)), 0)


if __name__ == '__main__':
    unittest.main()
