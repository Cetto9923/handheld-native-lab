from pathlib import Path
import subprocess,tempfile,re,sys
s=Path(sys.argv[1]).read_text() if len(sys.argv)>1 else (Path(__file__).resolve().parents[2]/'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
start=s.index('struct ArenaLife {');end=s.index('// Independent allocation:',start)
actual=s[start:end]
harness=r'''
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <cstdarg>
#include <cstdio>
using VkBuffer=uint64_t;using VkDeviceMemory=uint64_t;using VkDeviceSize=uint64_t;
constexpr int VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO=1,VK_SHARING_MODE_EXCLUSIVE=0,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT=1,VK_MEMORY_PROPERTY_HOST_CACHED_BIT=2,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT=4,VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO=2;
constexpr uint64_t VK_WHOLE_SIZE=~0ull,arena_usage=3,arena_chunk_bytes=64,arena_alignment=8;
struct {int device;}cfg;
struct Type{unsigned propertyFlags=7,heapIndex=0;};struct{Type memoryTypes[8];}memories;
struct VkBufferCreateInfo {int sType;uint64_t size=0,usage=0;int sharingMode=0;};
struct VkMemoryRequirements {uint64_t size;uint32_t memoryTypeBits;};
struct VkMemoryAllocateInfo {int sType;uint64_t allocationSize=0;uint32_t memoryTypeIndex=0;};
static uint64_t next=0;static unsigned creates=0,destroys=0,frees=0,maps=0,unmaps=0,fail=0;static unsigned char backing[4096];
void need(bool yes,const char*){if(!yes)throw std::runtime_error("need");}
#define CPU_SCOPE(x) ((void)0)
#define VK(x) need((x)==0,"VK")
std::vector<std::string> logs;void xbg_logf(const char *fmt,...){char b[2048];va_list a;va_start(a,fmt);vsnprintf(b,sizeof b,fmt,a);va_end(a);logs.push_back(b);}
uint32_t find_memory(uint32_t,int,int=0){return 0;}
int vkCreateBuffer(int,const VkBufferCreateInfo*,void*,VkBuffer *p){*p=++next;++creates;return fail==1?-1:0;}
void vkGetBufferMemoryRequirements(int,VkBuffer,VkMemoryRequirements *p){*p={512,1};}
int vkAllocateMemory(int,const VkMemoryAllocateInfo*,void*,VkDeviceMemory *p){*p=++next;return fail==2?-1:0;}
int vkBindBufferMemory(int,VkBuffer,VkDeviceMemory,int){return 0;}
int vkMapMemory(int,VkDeviceMemory,int,uint64_t,int,void **p){*p=backing;++maps;return fail==3?-1:0;}
void vkUnmapMemory(int,VkDeviceMemory){++unmaps;}void vkDestroyBuffer(int,VkBuffer,void*){++destroys;}void vkFreeMemory(int,VkDeviceMemory,void*){++frees;}void memo_forget_buffer(VkBuffer){}
struct ArenaChunk{VkBuffer buffer{};VkDeviceMemory memory{};uint8_t *mapped{};VkDeviceSize size=0,used=0,allocation_size=0;bool coherent=true;};
struct Buffer{VkBuffer buffer{};VkDeviceMemory memory{};VkDeviceSize offset=0;size_t size{};void *mapped{};bool coherent=true;VkDeviceSize allocation_size=0;};
'''
original='struct OldArena {\n    std::vector<ArenaChunk> chunks;size_t current=0;bool cached=false;VkDeviceSize used_total=0,peak=0;\n    ArenaChunk make(VkDeviceSize bytes){\n        CPU_SCOPE(Buffer);ArenaChunk c;c.size=bytes;\n        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=arena_usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;VK(vkCreateBuffer(cfg.device,&ci,nullptr,&c.buffer));\n        VkMemoryRequirements r;vkGetBufferMemoryRequirements(cfg.device,c.buffer,&r);\n        uint32_t type=cached?find_memory(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT):find_memory(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);\n        if(cached && !(memories.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT))type=find_memory(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_CACHED_BIT);\n        c.coherent=(memories.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;\n        {static int logged[2];if(!logged[cached]++)xbg_logf("NVN arena chunk cached=%d memoryType=%u flags=%#x heap=%u",int(cached),type,unsigned(memories.memoryTypes[type].propertyFlags),memories.memoryTypes[type].heapIndex);}\n        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=r.size;ai.memoryTypeIndex=type;c.allocation_size=r.size;\n        VK(vkAllocateMemory(cfg.device,&ai,nullptr,&c.memory));VK(vkBindBufferMemory(cfg.device,c.buffer,c.memory,0));\n        void *p=nullptr;VK(vkMapMemory(cfg.device,c.memory,0,VK_WHOLE_SIZE,0,&p));need(p!=nullptr,"vkMapMemory returned null pointer");c.mapped=static_cast<uint8_t*>(p);return c;\n    }\n    Buffer alloc(size_t n){\n        need(n && n<=256ull*1024*1024,"Invalid or excessive buffer size");\n        for(size_t i=current;;++i){\n            if(i==chunks.size())chunks.push_back(make(std::max<VkDeviceSize>(arena_chunk_bytes,(n+arena_alignment-1)&~(arena_alignment-1))));\n            ArenaChunk &c=chunks[i];VkDeviceSize at=(c.used+arena_alignment-1)&~(arena_alignment-1);\n            if(at+n>c.size)continue;\n            current=i; // leftovers in skipped chunks are abandoned until reset\n            c.used=at+n;used_total+=n;peak=std::max(peak,used_total);\n            need(c.mapped!=nullptr,"ArenaChunk memory is unmapped");\n            Buffer b;b.buffer=c.buffer;b.memory=c.memory;b.offset=at;b.size=n;b.mapped=c.mapped+at;b.coherent=c.coherent;b.allocation_size=c.allocation_size;return b;\n        }\n    }\n    // reset() keeps all memory mapped (a synchronous reader may still copy out\n    // of it); trim() releases oversized/extra chunks before the next recording.\n    size_t keep_chunks=3;\n    void reset(){for(auto &c:chunks)c.used=0;current=0;used_total=0;}\n    template<class Protected> void trim(Protected protect){\n        size_t kept=0;std::vector<ArenaChunk> keep;\n        for(auto &c:chunks){\n            if(protect(c.buffer))keep.push_back(c);\n            else if(c.size==arena_chunk_bytes && kept<keep_chunks){keep.push_back(c);++kept;}\n            else destroy(c);\n        }\n        chunks.swap(keep);current=0;\n    }\n    void trim(){trim([](VkBuffer){return false;});}\n    static void destroy(ArenaChunk &c){if(c.buffer)memo_forget_buffer(c.buffer);if(c.mapped)vkUnmapMemory(cfg.device,c.memory);if(c.buffer)vkDestroyBuffer(cfg.device,c.buffer,nullptr);if(c.memory)vkFreeMemory(cfg.device,c.memory,nullptr);c=ArenaChunk{};}\n    void release(){for(auto &c:chunks)destroy(c);chunks.clear();current=0;used_total=0;}\n};\n'
agg=s[s.index('ArenaLife arena_life_before['):s.index('static bool xbg_writeback_is_current_slot',s.index('ArenaLife arena_life_before['))]
agg='constexpr unsigned kMaxSlots=2;struct MockSlot{Arena stream,uniform,readback;};MockSlot slots[kMaxSlots];\n'+agg
main=r'''
int main(){
 for(size_t limit:{1u,2u}){
  Arena r;r.keep_chunks=limit;for(int i=0;i<4;++i)r.alloc(64);r.alloc(80);auto guard=r.chunks[3].buffer;r.reset();
  r.trim([&](VkBuffer b){return b==guard;});assert(r.chunks.size()==limit+1);assert(r.life.kind[1].trimmed==1);
  auto saved=r.keep_chunks;r.keep_chunks=0;r.trim([&](VkBuffer b){return b==guard;});r.keep_chunks=saved;
  assert(r.chunks.size()==1 && r.chunks[0].buffer==guard && r.keep_chunks==limit);
  assert(r.life.kind[0].forced==limit);r.release();
 }
 for(size_t limit:{1u,2u}){
  Arena r;r.keep_chunks=limit;r.alloc(64);r.alloc(64);auto initial=r.life.kind[0].made;
  for(int cycle=0;cycle<3;++cycle){r.reset();r.trim();r.alloc(64);r.alloc(64);}
  assert(r.life.kind[0].made-initial==(limit==1?3:0));r.release();
 }

 slots[0].stream.life.kind[0].made=10;slots[0].stream.life.kind[0].make_bytes=640;
 slots[1].uniform.life.kind[1].made=5;
 slots[0].readback.life.kind[1].trimmed=2;
 arena_life_start();
 slots[0].stream.life.kind[0].made+=2;slots[0].stream.life.kind[0].make_bytes+=128;
 slots[1].stream.life.kind[0].made+=3;slots[1].stream.life.kind[0].make_bytes+=192;
 slots[1].uniform.life.kind[1].made=4; // reset, excluded rather than underflow
 slots[0].uniform.life.kind[1].made=7;
 slots[0].readback.life.kind[1].trimmed+=3;slots[1].readback.life.kind[0].protected_kept=2;
 logs.clear();arena_life_report();assert(logs.size()==6);
 assert(logs[0].find("arena=stream class=standard")!=std::string::npos && logs[0].find("make_calls=5 make_bytes=320")!=std::string::npos);
 assert(logs[3].find("arena=uniform class=oversize")!=std::string::npos && logs[3].find("make_calls=7")!=std::string::npos && logs[3].find("invalid_slots=1")!=std::string::npos);
 assert(logs[5].find("trim_calls=3")!=std::string::npos);
 assert(logs[4].find("protected_keep_calls=2")!=std::string::npos);
 arena_life_start();logs.clear();arena_life_report();for(auto &l:logs)assert(l.find("make_calls=0")!=std::string::npos && l.find("trim_calls=0")!=std::string::npos && l.find("invalid_slots=0")!=std::string::npos);

 for(size_t keep:{0u,1u,2u,3u})for(bool protect:{false,true}){
  Arena a;OldArena b;a.keep_chunks=b.keep_chunks=keep;
  for(size_t n:{8u,56u,64u,80u,96u}){auto x=a.alloc(n),y=b.alloc(n);assert(x.size==y.size && x.offset==y.offset && x.allocation_size==y.allocation_size && a.current==b.current && a.used_total==b.used_total);}
  auto aid=a.chunks[2].buffer,bid=b.chunks[2].buffer;a.reset();b.reset();
  a.trim([&](VkBuffer v){return protect&&v==aid;});b.trim([&](VkBuffer v){return protect&&v==bid;});
  assert(a.chunks.size()==b.chunks.size() && a.current==b.current && a.used_total==b.used_total);
  for(size_t i=0;i<a.chunks.size();++i)assert(a.chunks[i].size==b.chunks[i].size && a.chunks[i].used==b.chunks[i].used && a.chunks[i].allocation_size==b.chunks[i].allocation_size);
  a.release();b.release();assert(a.chunks.size()==b.chunks.size());
 }

 for(unsigned f:{1u,2u,3u}){Arena a;fail=f;try{a.make(64);assert(false);}catch(const std::runtime_error&){}assert(!a.life.kind[0].made && !a.life.kind[1].made);}fail=0;
 for(size_t keep:{0u,1u,2u,3u})for(bool protect:{false,true}){
  Arena a;a.keep_chunks=keep;a.alloc(64);a.alloc(64);a.alloc(80);a.alloc(96);assert(a.life.kind[0].made==2 && a.life.kind[0].make_bytes==128 && a.life.kind[0].make_alloc==1024);assert(a.life.kind[1].made==2 && a.life.kind[1].make_bytes==176 && a.life.kind[1].make_alloc==1024);
  auto protected_id=a.chunks[2].buffer;unsigned d=destroys,u=unmaps,f=frees;a.reset();a.trim([&](VkBuffer b){return protect && b==protected_id;});
  size_t std_kept=std::min<size_t>(keep,2);assert(a.chunks.size()==std_kept+protect);assert(a.life.kind[0].trimmed==2-std_kept);assert(a.life.kind[0].trim_bytes==(2-std_kept)*64);assert(a.life.kind[1].trimmed==2-protect);assert(a.life.kind[1].trim_bytes==(protect?96:176));assert(a.life.kind[1].protected_kept==protect);assert(a.life.kind[0].forced==(keep?0:2));assert(a.life.kind[1].forced==(keep?0:2-protect));
  assert(destroys-d==4-std_kept-protect && unmaps-u==destroys-d && frees-f==destroys-d);assert(a.current==0);
  auto before=a.life;a.release();assert(a.life.kind[0].trimmed==before.kind[0].trimmed && a.life.kind[1].trimmed==before.kind[1].trimmed);assert(a.chunks.empty());
 }
 Arena a;a.alloc(80);a.reset();a.trim();a.alloc(80);assert(a.life.kind[1].made==2 && a.life.kind[1].trimmed==1); // observation only, no retention
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(harness+original+actual+agg+main)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
assert 'life.make(c.size,c.allocation_size,arena_chunk_bytes);return c;' in actual
assert 'else {life.trim(c.size,c.allocation_size,arena_chunk_bytes,keep_chunks==0);destroy(c);}' in actual
assert 'if(protect(c.buffer)){life.protect(c.size,arena_chunk_bytes);keep.push_back(c);}' in actual
assert 'void timing_start(){\n    arena_life_start();' in s
assert 'arena_life_report();\n    descriptor_report();' in s
print('PASS actual Arena make/alloc/reset/trim/release with Vulkan mock: ASan/UBSan; failed make, standard/oversize, keep0, protected, repeated allocation; device NOT_RUN')

pressure='        const size_t keep = slot.readback.keep_chunks;\n        slot.readback.keep_chunks = 0;\n        slot.readback.trim([&](VkBuffer buffer) {\n            return std::any_of(slot.guard.begin(), slot.guard.end(),\n                               [&](const Slot::XRange &range) { return range.buffer == buffer; });\n        });\n        slot.readback.keep_chunks = keep;\n'
assert pressure in s

assert 'size_t readback_keep_standard=1;' in s
assert 'fscanf(m,"%u",&value)==1 && value==2' in s
assert s.count('gfx-readback-keep-standard')==1
assert 's.stream.trim();s.uniform.keep_chunks=1;s.uniform.trim();s.readback.keep_chunks=readback_keep_standard;' in s
record=s[s.index('Slot &recording_slot(){'):s.index('// Submit the recording slot.')]
assert record.index('retire_slot(s)')<record.index('resolve_pending();break;')<record.index('s.readback.keep_chunks=readback_keep_standard;')<record.index('s.readback.trim(')
assert 'return std::any_of(s.guard.begin(),s.guard.end(),' in record
print('PASS keep1/keep2 retention churn and protected 4-standard+oversize, pressure keep0 override/restoration; original sync/order source contract')
