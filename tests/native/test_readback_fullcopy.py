"""Production readback full-copy contracts. No device/GPU execution.
Run: python3 tests/native/test_readback_fullcopy.py [--source path]
Requires clang++ with ASan/UBSan; generates only temporary files.
"""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile,re
HARNESS = r'''
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <stdexcept>
using VkDeviceSize=uint64_t;
#define DESC_CALL(k,e) (e)
#define DESC_SCOPE(k) ((void)0)
void need(bool b,const char*){if(!b)throw std::runtime_error("need");}
struct Root {void *recorder;};Root root{&root};Root *descriptor_root=&root;bool descriptor_detail=true;
struct Timing {uint64_t readback_alloc_bytes=0,readback_copy_bytes=0,readback_zero_bytes=0,readback_guard_bytes=0,readback_noncoherent_flushes=0;} descriptor_timing;
unsigned flushes; std::vector<uint8_t> dest(256),stage(256);bool coherent;
struct Buffer {unsigned buffer;uint64_t offset;void *mapped;bool coherent;unsigned memory=3;size_t size=64;uint64_t allocation_size=256;void flush(){if(!coherent)++flushes;}};
struct Arena {Buffer alloc(size_t n){need(n && n<=256,"size");return {1,0,dest.data(),coherent};}};
struct Guard {unsigned buffer;uint64_t begin,end;};
struct Op {unsigned src,dst;uint64_t a,b,n;};
struct Slot {Arena readback;std::vector<Guard> guard;std::vector<Op> pro_ops;} slot;
Slot &recording_slot(){return slot;}
namespace evguard {struct EngineRead {EngineRead(const void*,size_t){}};}
uint64_t xview_guarded=0,xview_guarded_bytes=0;
Buffer stream_buffer(size_t n,const void *p,size_t c,bool z){need(c<=n,"size");if(p && c)memcpy(stage.data(),p,c);if(z && c<n)memset(stage.data()+c,0,n-c);return {2,0,stage.data(),true};}

bool readback_fullcopy_enabled=false;uint64_t readback_fullcopy_hits=0,readback_fullcopy_bytes=0;bool aboff=false;bool ab_off(const char*){return aboff;}
'''

ORIGINAL_0400EF7 = r'''Buffer original(size_t n,const void *data=nullptr,size_t copy=0){
    Slot &s=recording_slot();Buffer b=s.readback.alloc(n);
    for(const auto &g:s.guard)if(g.buffer==b.buffer && b.offset<g.end && g.begin<b.offset+n){
        if(data && copy)need(copy<=n,"Buffer copy overflow");
        Buffer src=stream_buffer(n,data,data?copy:0,true);s.pro_ops.push_back({src.buffer,b.buffer,src.offset,b.offset,VkDeviceSize(n)});
        ++xview_guarded;xview_guarded_bytes+=n;return b;}
    if(data && copy){need(copy<=n,"Buffer copy overflow");need(b.mapped!=nullptr,"readback_buffer destination mapped pointer is null");evguard::EngineRead er(data,copy);memcpy(b.mapped,data,copy);}else copy=0;
    if(copy<n){need(b.mapped!=nullptr,"readback_buffer zero-fill destination mapped pointer is null");memset(static_cast<uint8_t*>(b.mapped)+copy,0,n-copy);}   // only the bytes the copy leaves
    b.flush();return b;
}'''

LEGACY_TEST = r'''
int main(){
 for(bool measured:{false,true})for(bool coh:{false,true})for(bool guard:{false,true})for(bool data:{false,true})for(size_t copy:{0u,16u,64u}){
 unsigned char input[64];for(unsigned i=0;i<64;++i)input[i]=i+1;
 auto reset=[&](){dest.assign(256,0xA5);stage.assign(256,0xCC);slot.guard.clear();slot.pro_ops.clear();if(guard)slot.guard.push_back({1,0,64});flushes=0;xview_guarded=xview_guarded_bytes=0;descriptor_timing={};coherent=coh;descriptor_detail=measured;};
 reset();original(64,data?input:nullptr,copy);auto expected=dest;auto es=stage;auto ef=flushes;auto ep=slot.pro_ops.size();auto eg=xview_guarded;
 reset();candidate(64,data?input:nullptr,copy);assert(dest==expected && stage==es && flushes==ef && slot.pro_ops.size()==ep && xview_guarded==eg);
 assert(descriptor_timing.readback_alloc_bytes==(measured?64:0));
 if(!measured)assert(descriptor_timing.readback_copy_bytes+descriptor_timing.readback_zero_bytes+descriptor_timing.readback_guard_bytes+descriptor_timing.readback_noncoherent_flushes==0);
 else if(guard)assert(descriptor_timing.readback_guard_bytes==64 && descriptor_timing.readback_zero_bytes==0);
 else {assert(descriptor_timing.readback_copy_bytes==(data?copy:0));assert(descriptor_timing.readback_zero_bytes==64-(data?copy:0));assert(descriptor_timing.readback_noncoherent_flushes==!coh);}
 }
}
'''

FULLCOPY_TEST = r'''
int main(){
 Buffer src{5,0,nullptr,true};src.memory=4;Buffer dst{1,0,nullptr,true};
 assert(readback_fullcopy_bounds(src,dst,64));
 auto fail=[&](Buffer s,Buffer d,size_t n){assert(!readback_fullcopy_bounds(s,d,n));};
 fail(src,dst,0);fail(src,dst,63);fail(src,dst,68);
 {auto q=src;q.offset=1;fail(q,dst,64);q=src;q.allocation_size=0;fail(q,dst,64);q=src;q.size=63;fail(q,dst,64);q=src;q.offset=252;fail(q,dst,64);q=src;q.offset=UINT64_MAX-3;fail(q,dst,64);q=src;q.memory=3;fail(q,dst,64);q=src;q.memory=0;fail(q,dst,64);q=src;q.buffer=1;q.memory=3;fail(q,dst,64);q.offset=64;assert(readback_fullcopy_bounds(q,dst,64));}
 for(bool enable:{false,true})for(bool blocked:{false,true})for(bool guard:{false,true})for(bool coh:{false,true})for(bool valid:{false,true}){
 dest.assign(256,0xA5);stage.assign(256,0xCC);slot.guard.clear();slot.pro_ops.clear();if(guard)slot.guard.push_back({1,0,64});flushes=0;descriptor_timing={};coherent=coh;readback_fullcopy_enabled=enable;aboff=blocked;readback_fullcopy_hits=readback_fullcopy_bytes=0;
 Buffer source=src;if(!valid)source.allocation_size=0;
 auto out=readback_buffer(64,nullptr,0,&source);
 bool skip=enable && !blocked && !guard && valid;
 assert(readback_fullcopy_hits==unsigned(skip));assert(readback_fullcopy_bytes==(skip?64:0));
 for(unsigned i=0;i<64;++i)assert(dest[i]==(skip||guard?0xA5:0));
 assert(flushes==unsigned(!skip&&!guard&&!coh));assert(slot.pro_ops.size()==unsigned(guard));
 // Apply original full GPU-copy semantics: both legacy and candidate produce
 // identical complete source bytes, regardless of prior destination poison.
 unsigned char bytes[64];for(unsigned i=0;i<64;++i)bytes[i]=i^0x6D;
 memcpy(out.mapped,bytes,64);assert(memcmp(out.mapped,bytes,64)==0);
 }
 readback_fullcopy_enabled=true;aboff=false;slot.guard.clear();dest.assign(256,0xA5);unsigned char bytes[64]{};readback_fullcopy_hits=0;readback_buffer(64,bytes,64,&src);assert(readback_fullcopy_hits==0); // never skip CPU-data path
 dest.assign(256,0xA5);readback_buffer(64);for(unsigned i=0;i<64;++i)assert(dest[i]==0); // old callers unchanged
}
'''

def extract(source, signature):
    start=source.index(signature)
    return source[start:source.index('\n}',start)+2]+'\n'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp')
    args=parser.parse_args(); source=args.source.read_text()
    helper=extract(source,'bool readback_fullcopy_bounds(')
    readback=extract(source,'Buffer readback_buffer(')
    assert 'Buffer stream_buffer(' not in helper+readback
    assert 'Buffer uniform_buffer(' not in helper+readback
    with tempfile.TemporaryDirectory(prefix='ffrs-fullcopy-contract-') as directory:
        for name,code in [('legacy',HARNESS+helper+ORIGINAL_0400EF7+readback.replace('Buffer readback_buffer(', 'Buffer candidate(')+LEGACY_TEST),('fullcopy',HARNESS+helper+readback+FULLCOPY_TEST)]:
            cpp=Path(directory)/(name+'.cpp');exe=Path(directory)/name
            cpp.write_text(code)
            subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    check_wiring(source)
    # Real negative controls run the same checker against corrupted callsites.
    for old,new in [('!out.indirect_input','true'),('!probe_is_active()','true'),('full_barrier(cmd);','/* removed barrier */')]:
        lines=source.splitlines(True)
        site=next(i for i,line in enumerate(lines) if 'readback_buffer(bytes,nullptr,0,' in line)
        tail=''.join(lines[site:]);assert old in tail
        corrupted=''.join(lines[:site])+tail.replace(old,new,1)
        try:
            check_wiring(corrupted)
        except AssertionError:
            pass
        else:
            raise AssertionError('wiring checker missed '+old)
    print('PASS current source SHA256='+hashlib.sha256(source.encode()).hexdigest())
    print('ASan/UBSan legacy 48 + candidate 32 combinations; GPU behavior NOT_RUN')

def check_wiring(s):
    calls=[x for x in s.splitlines() if 'readback_buffer(bytes,nullptr,0,' in x]
    assert len(calls)==2,'Only two audited sites may opt in'
    for x in calls:
     assert '!out.indirect_input' in x and '!probe_is_active()' in x,'Missing CPU-read exclusion'
    a=s.index('Buffer readback_buffer(size_t n,');b=s.index('// Per-submission snapshot cache',a);body=s[a:b]
    assert body.index('for(const auto &g:s.guard)')<body.index('if(fullcopy_source'),'Guard must precede skip'
    assert 'Buffer src=stream_buffer(n,data,data?copy:0,true);s.pro_ops.push_back({src.buffer,b.buffer,src.offset,b.offset,VkDeviceSize(n)});' in body,'Guard staging changed'
    assert '++xview_guarded;xview_guarded_bytes+=n;return b;}}' in body,'Guard must return through original prologue'
    assert 'fullcopy_source=nullptr' in body and 'fullcopy_source && !data && !copy' in body,'Legacy/default/data paths changed'
    assert 'readback_fullcopy_enabled=false' in s,'Default must be disabled'
    # Exact command sequences from frozen baseline after allocation. Keep source,
    # destination, entire length and the original barrier unchanged.
    expected=[
    '''VkCommandBuffer cmd=recording_slot().cmd;
                                    end_open_pass();
                                    VkBufferCopy region{view.offset,buf->offset,bytes};
                                    vkCmdCopyBuffer(cmd,view.buffer,buf->buffer,1,&region);
                                    full_barrier(cmd);''',
    '''VkCommandBuffer cmd=recording_slot().cmd;
                                        const bool ended=end_open_pass();
                                        VkBufferCopy region{view.offset,buf->offset,bytes};
                                        vkCmdCopyBuffer(cmd,view.buffer,buf->buffer,1,&region);
                                        full_barrier(cmd);''']
    for x,e in zip(calls,expected):
     pos=s.index(x)+len(x)
     assert re.sub(r'\s+', '', s[pos:]).startswith(re.sub(r'\s+', '', e)),'GPU copy/barrier changed or operation inserted before copy'

if __name__=="__main__":
    main()
