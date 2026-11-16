"""Exercise production texel residency across bounded in-submission writes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TexelRangeValidation(unittest.TestCase):
    def test_content_checks_and_pending_write_fallbacks(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        resident = source[source.index('struct TexelKey{'):
                          source.index('// Only copied, read-only graphics inputs')]
        journal = source[source.index('struct TexelWriteRange{'):
                         source.index('void texel_evict(unsigned si,bool all);')]
        functions = source[source.index('bool texel_range_unchanged('):
                           source.index('// While a slot starts recording')]
        hashing = source[source.index('uint64_t hash_memory('):
                         source.index('void invalidate_cpu_range(')]
        reset = next(line for line in source.splitlines()
                     if line.startswith('void snapshot_reset(){'))
        fixture = r'''
#include "xbg_writeback_policy.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>
using VkFormat=uint32_t;
using VkBufferView=uint64_t;
struct Buffer {
    std::shared_ptr<std::vector<uint8_t>> owner;
    void *mapped=nullptr;
    uint64_t buffer=0,offset=0;
    void flush() {}
};
struct Arena {
    Buffer alloc(size_t bytes) {
        auto owner=std::make_shared<std::vector<uint8_t>>(bytes);
        return {owner,owner->data(),1,0};
    }
};
constexpr unsigned kMaxSlots=16;
struct Slot { bool recording=true,pending=false; } slots[kMaxSlots];
unsigned slot_index=0;
uint64_t slot_serial=1;
struct PendingWrite { uint64_t addr,bytes; };
struct PendingEvent { uint64_t addr; };
std::vector<PendingWrite> pending_writebacks;
std::vector<PendingEvent> pending_reports,pending_events,recording_events;
bool honor_inline_enabled=true,old_path=false;
bool ab_off(const char *name) { assert(!strcmp(name,"texelrange"));return old_path; }
void need(bool condition,const char *) { assert(condition); }
#define CPU_SCOPE(name) ((void)0)
#define VK(call) assert((call)==0)
enum { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO=1 };
struct VkBufferViewCreateInfo {
    int sType; uint64_t buffer=0,offset=0; VkFormat format=0; uint64_t range=0;
};
struct Config { int device=0; } cfg;
int vkCreateBufferView(int,const VkBufferViewCreateInfo *,void *,VkBufferView *view) {
    static uint64_t next_view=1;*view=next_view++;return 0;
}
struct CopyCache { void reset() {} } texel_copy_cache;
uint64_t snapshot_maxlen=0,snapshot_epoch=1;
void snapshot_maps_reset() {}
void desc_memo_reset() {}
void last_pass_reset() {}
'''
        fixture += resident + journal + hashing + reset + '\n' + functions
        fixture += r'''
int main() {
    std::vector<uint8_t> memory(4096,7);
    uint64_t base=reinterpret_cast<uintptr_t>(memory.data());
    TexelResident *original=texel_acquire(base,1024,1);
    assert(original && texel_hash_bytes==1024);
    assert(texel_acquire(base,1024,1)==original && texel_hash_bytes==1024);
    // An overlapping small resident invalidates the old global epoch, while
    // the larger resident below remains byte-for-byte unchanged.
    assert(texel_acquire(base+2048,64,1));
    uint64_t hashed=texel_hash_bytes;
    texel_touch(base+2048,4);
    assert(texel_acquire(base,1024,1)==original);
    assert(texel_hash_bytes==hashed && texel_range_hits==1);
    // Real overlapping mutations must never reuse an earlier in-flight copy.
    memory[1023]^=1;texel_touch(base+1023,1);
    assert(!texel_acquire(base,1024,1));
    assert(texel_hash_bytes==hashed+1024);
    assert(static_cast<uint8_t*>(original->buffer.mapped)[1023]==7);
    // Once this slot is retired it can refresh its own allocation.
    ++slot_serial;snapshot_reset();
    assert(texel_acquire(base,1024,1)==original);
    assert(!memcmp(original->buffer.mapped,memory.data(),1024));
    // All submissions revalidate even without an explicit in-stream write.
    memory[0]^=1;hashed=texel_hash_bytes;snapshot_reset();
    assert(!texel_acquire(base,1024,1));
    assert(texel_hash_bytes==hashed+1024);
    ++slot_serial;snapshot_reset();assert(texel_acquire(base,1024,1));
    // A full 64-entry history is safe, a missed entry forces a full check.
    hashed=texel_hash_bytes;
    for(unsigned i=0;i<64;++i)texel_touch(base+2048,1);
    assert(texel_acquire(base,1024,1) && texel_hash_bytes==hashed);
    for(unsigned i=0;i<65;++i)texel_touch(base+2048,1);
    assert(texel_acquire(base,1024,1) && texel_hash_bytes==hashed+1024);
    // Partial overlaps, including an earlier write in the ring, are retained.
    memory[0]^=1;texel_touch(base,1);
    for(unsigned i=0;i<63;++i)texel_touch(base+2048,1);
    assert(!texel_acquire(base,1024,1));
    ++slot_serial;snapshot_reset();assert(texel_acquire(base,1024,1));
    // Pending writes, timestamp reports and both event queues still win over
    // a previously verified resident without touching the journal at all.
    pending_writebacks.push_back({base+1023,4});
    assert(!texel_acquire(base,1024,1));pending_writebacks.clear();
    pending_reports.push_back({base-8});
    assert(!texel_acquire(base,1024,1));pending_reports.clear();
    pending_events.push_back({base+1023});
    assert(!texel_acquire(base,1024,1));pending_events.clear();
    recording_events.push_back({base});
    assert(!texel_acquire(base,1024,1));recording_events.clear();
    // Fallback profiles and the live A/B arm retain the full hash path.
    for(unsigned mode=0;mode<2;++mode){
        honor_inline_enabled=mode!=0;old_path=mode==1;
        texel_touch(base+2048,1);hashed=texel_hash_bytes;
        assert(texel_acquire(base,1024,1) && texel_hash_bytes==hashed+1024);
    }
    honor_inline_enabled=true;old_path=false;
    // The >64-resident path records unrelated writes without a full scan.
    for(unsigned i=0;i<65;++i)
        texel_resident[0].emplace(TexelKey{base+4096+i*8,4,1},TexelResident{});
    assert(texel_resident[0].size()>64);
    hashed=texel_hash_bytes;texel_touch(base+4096+8*64,1);
    assert(texel_acquire(base,1024,1) && texel_hash_bytes==hashed);
    uint64_t epoch=texel_write_epoch;
    texel_touch(base,0);assert(texel_write_epoch==epoch);
    texel_touch(UINT64_MAX-1,4);hashed=texel_hash_bytes;
    assert(texel_acquire(base,1024,1) && texel_hash_bytes==hashed+1024);
    assert(!texel_range_unchanged({base,1024,1},texel_write_epoch+1));
    // Separate slots and view formats still keep independent storage.
    slot_index=1;++slot_serial;snapshot_reset();
    auto *other=texel_acquire(base,1024,1);
    assert(other && other->buffer.mapped!=original->buffer.mapped);
    auto *typed=texel_acquire(base,1024,2);
    assert(typed && typed->view!=other->view);
    assert(!texel_acquire(UINT64_MAX-8,16,1));
    assert(!texel_acquire(base,0,1));
    assert(!texel_acquire(base,64ull*1024*1024+1,1));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-Wno-missing-field-initializers',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'src/graphics/nvn_vulkan/engine'),
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
