"""Exercise production uniform-upload routing and its slot lifecycle wiring."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp'


class UniformArena(unittest.TestCase):
    def run_uploads(self, restore_mixed=False):
        source = SOURCE.read_text()
        start = source.index('Buffer stream_buffer(size_t n,')
        body = source[start:source.index('Buffer readback_buffer(', start)]
        if restore_mixed:
            body = body.replace('?s.uniform:s.stream', '?s.stream:s.stream')
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
unsigned flushes=0,reads=0,recordings=0;
uint64_t next_buffer=0;
void need(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
namespace evguard {
struct EngineRead { EngineRead(const void *,size_t) { ++reads; } };
}
struct Buffer {
    uint64_t buffer;
    size_t offset,size;
    void *mapped;
    void flush() { ++flushes; }
};
struct Arena {
    struct Chunk {
        uint64_t buffer;
        std::unique_ptr<uint8_t[]> data;
        size_t size,used;
    };
    std::vector<Chunk> chunks;
    Buffer alloc(size_t n) {
        if(chunks.empty() || chunks.back().used+n>chunks.back().size) {
            size_t bytes=std::max<size_t>(4096,n);
            auto memory=std::make_unique<uint8_t[]>(bytes);
            memset(memory.get(),0xa5,bytes);
            chunks.push_back({++next_buffer,std::move(memory),bytes,0});
        }
        Chunk &chunk=chunks.back();size_t offset=chunk.used;chunk.used+=n;
        return {chunk.buffer,offset,n,chunk.data.get()+offset};
    }
};
struct Slot { Arena stream,uniform; } slots[2];
unsigned slot_index=0;
Slot &recording_slot() { ++recordings;return slots[slot_index]; }
bool honor_inline_enabled=true,control=false;
bool ab_off(const char *name) { assert(std::string(name)=="uboarena");return control; }
''' + body + r'''
int main() {
    const uint8_t payload[]={1,2,3,4,5};
    for(unsigned slot=0;slot<2;++slot) {
        slot_index=slot;
        Buffer first=uniform_buffer(16,payload,sizeof(payload));
        assert(memcmp(first.mapped,payload,sizeof(payload))==0);
        for(size_t i=sizeof(payload);i<first.size;++i)
            assert(static_cast<uint8_t *>(first.mapped)[i]==0);
        Buffer large=stream_buffer(8192,payload,sizeof(payload),false);
        assert(large.buffer!=first.buffer);
        assert(memcmp(large.mapped,payload,sizeof(payload))==0);
        assert(static_cast<uint8_t *>(large.mapped)[5]==0xa5);
        Buffer second=uniform_buffer(32);
        assert(second.buffer==first.buffer && second.offset>=first.offset+first.size);
        assert(memcmp(first.mapped,payload,sizeof(payload))==0);
        for(size_t i=0;i<second.size;++i)
            assert(static_cast<uint8_t *>(second.mapped)[i]==0);
    }
    assert(slots[0].uniform.chunks[0].buffer!=slots[1].uniform.chunks[0].buffer);
    slot_index=0;
    for(bool honor:{false,true}) {
        honor_inline_enabled=honor;control=honor;
        Buffer old=uniform_buffer(16,payload,sizeof(payload));
        assert(old.buffer==slots[0].stream.chunks.back().buffer);
    }
    honor_inline_enabled=true;control=false;
    Buffer copy=uniform_buffer(16,payload,sizeof(payload));
    assert(copy.buffer==slots[0].uniform.chunks.back().buffer);
    assert(flushes==9 && reads==7 && recordings==9);
    bool failed=false;
    try { uniform_buffer(4,payload,sizeof(payload)); }
    catch(const std::runtime_error &) { failed=true; }
    assert(failed && flushes==9 && reads==7);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'fixture.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'fixture.cpp'),
                            '-o', str(path / 'fixture')], check=True, capture_output=True)
            return subprocess.run([str(path / 'fixture')], capture_output=True)

    def test_upload_bytes_and_isolation_from_vertex_growth(self):
        result = self.run_uploads()
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_mixed_arena_regression_is_detected(self):
        result = self.run_uploads(restore_mixed=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('second.buffer==first.buffer', result.stderr.decode())

    def test_slot_retirement_budget_and_all_uniform_entry_points(self):
        source = SOURCE.read_text()
        self.assertLess(source.index('extern bool honor_inline_enabled;'),
                        source.index('Buffer uniform_buffer(size_t n,'))
        retire = source[source.index('void retire_slot(Slot &s){'):
                        source.index('Slot &recording_slot(){')]
        self.assertLess(retire.index('hybrid_wait_fence('), retire.index('s.uniform.reset()'))
        self.assertIn('s.stream.peak+s.uniform.peak+s.readback.peak', retire)
        recording = source[source.index('Slot &recording_slot(){'):
                           source.index('void submit_slots(')]
        self.assertLess(recording.index('retire_slot(s)'), recording.index('s.uniform.trim()'))
        self.assertIn('s.uniform.keep_chunks=1;s.uniform.trim()', recording)
        self.assertIn('cur.stream.used_total+cur.uniform.used_total+cur.readback.used_total', source)
        self.assertIn('add_arena(s.uniform)', source)
        destroy = source[source.index('void destroy_slots(){'):]
        self.assertIn('s.uniform.release()', destroy)
        self.assertIn('Buffer b=uniform_buffer(n,reinterpret_cast<const void*>(address),copy)', source)
        self.assertIn('Buffer s=uniform_buffer(0x1000)', source)
        self.assertIn('Buffer b=uniform_buffer(uniform_range(copy),src.mapped,copy)', source)
        # Existing CPU range checks and GPU-resident snapshot selection remain upstream.
        snapshot = source[source.index('Buffer uniform_snapshot_slow(uint64_t address,size_t copy){'):
                          source.index('// Descriptor memo (iOS')]
        self.assertLess(snapshot.index('pending_view('), snapshot.index('cpu_access('))
        self.assertLess(snapshot.index('cpu_access('), snapshot.index('uniform_buffer('))


if __name__ == '__main__':
    unittest.main()
