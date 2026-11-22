"""Exercise the production descriptor finalization with counted Vulkan calls."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FreshDescriptorCache(unittest.TestCase):
    def run_finalization(self, restore_old=False):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('    const bool fresh_cache_skip = honor_inline_enabled && fresh_storage_output')
        end = source.index('// Render passes are cached by their full definition;', start)
        tail = source[start:end]
        if restore_old:
            tail = tail.replace('const bool use_cache = !fresh_cache_skip && !experiment_cache_skip;',
                                'const bool use_cache = true;', 1)
        fixture = r'''
#include <vulkan/vulkan.h>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
template<class T> T handle(uintptr_t value) { return reinterpret_cast<T>(value); }
union DescEntry { VkDescriptorImageInfo image; VkDescriptorBufferInfo buffer; VkBufferView view; };
struct LayoutEntry {
    VkDescriptorSetLayout layout=handle<VkDescriptorSetLayout>(10);
    VkDescriptorUpdateTemplate tmpl=VK_NULL_HANDLE;
    std::vector<uint64_t> tmpl_sig;
};
struct Descriptors { const LayoutEntry *entry; VkDescriptorSet set=VK_NULL_HANDLE; };
struct Slot { bool recording=true; std::map<std::vector<uint64_t>,VkDescriptorSet> set_cache; } slots[1];
Slot &slot=slots[0];
unsigned slot_index=0;
uint64_t slot_serial=1;
struct Payload {
    VkDescriptorBufferInfo buffer{};
    VkDescriptorImageInfo image{};
    VkBufferView view=VK_NULL_HANDLE;
};
VkDevice device=handle<VkDevice>(1);
struct Config { VkDevice device; } cfg{device};
bool honor_inline_enabled=true,forced_old=false,fail_template=false;
bool honor_set_cache_bypass_experiment=false;
// The production finalization reads the descriptor memo switches. The fixture has to provide them,
// or the extracted production code does not compile and the contract stops being checked at all.
bool desc_memo_enabled=true,desc_memo_compute_enabled=true;
struct Properties { uint32_t vendorID=0x5143,deviceID=0x43030b00,driverVersion=0x802fa02e; } properties;
uint64_t set_cache_fresh_skips=0,set_cache_experiment_skips=0,set_cache_hits=0,set_cache_misses=0;
uint64_t template_updates=0,plain_updates=0,desc_ns[6]{};
std::vector<uint64_t> set_key;
std::vector<VkWriteDescriptorSet> writes;
std::vector<VkDescriptorUpdateTemplateEntry> entries;
std::vector<Payload> updates;
uint64_t allocations=0;
uint64_t api_descriptor_writes=0;
void api_descriptor_write() {
    assert(updates.size()>api_descriptor_writes);
    ++api_descriptor_writes;
}
// The production finalization asks for two different switches. "setfresh" models the old path
// (the fresh-cache skip turned off); "setbypass" is the Honor set-cache experiment and stays on.
bool ab_off(const char *name) {
    assert(std::string(name)=="setfresh"||std::string(name)=="setbypass");
    return std::string(name)=="setfresh" && forced_old;
}
uint64_t perf_clock() { return 1; }
Slot &recording_slot() {
    if(!slot.recording) { slot.set_cache.clear();slot.recording=true;++slot_serial; }
    return slot;
}
VkDescriptorSet allocate_set(VkDescriptorSetLayout layout) {
    assert(layout==handle<VkDescriptorSetLayout>(10));
    return handle<VkDescriptorSet>(100+(++allocations));
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice input,const VkDescriptorUpdateTemplateCreateInfo *info,
    const VkAllocationCallbacks *,VkDescriptorUpdateTemplate *output) {
    assert(input==device && info->descriptorSetLayout==handle<VkDescriptorSetLayout>(10));
    if(fail_template) return VK_ERROR_OUT_OF_HOST_MEMORY;
    entries.assign(info->pDescriptorUpdateEntries,info->pDescriptorUpdateEntries+info->descriptorUpdateEntryCount);
    *output=handle<VkDescriptorUpdateTemplate>(50);
    return VK_SUCCESS;
}
void consume(uint32_t binding,VkDescriptorType type,const void *input) {
    assert(binding<3);
    if(updates.empty()) updates.emplace_back();
    Payload &payload=updates.back();
    if(type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) payload.buffer=*static_cast<const VkDescriptorBufferInfo*>(input);
    else if(type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) payload.image=*static_cast<const VkDescriptorImageInfo*>(input);
    else { assert(type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER); payload.view=*static_cast<const VkBufferView*>(input); }
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice input,VkDescriptorSet set,VkDescriptorUpdateTemplate update,const void *data) {
    assert(input==device && set && update==handle<VkDescriptorUpdateTemplate>(50));
    updates.emplace_back();
    for(const auto &entry:entries) {
        assert(entry.descriptorCount==1 && entry.stride==sizeof(DescEntry));
        consume(entry.dstBinding,entry.descriptorType,static_cast<const char*>(data)+entry.offset);
    }
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice input,uint32_t count,const VkWriteDescriptorSet *values,
    uint32_t copies,const VkCopyDescriptorSet *) {
    assert(input==device && copies==0);
    updates.emplace_back();
    for(uint32_t i=0;i<count;++i) {
        const auto &write=values[i];
        assert(write.dstSet && write.descriptorCount==1);
        const void *data=write.pBufferInfo?static_cast<const void*>(write.pBufferInfo):
            write.pImageInfo?static_cast<const void*>(write.pImageInfo):static_cast<const void*>(write.pTexelBufferView);
        consume(write.dstBinding,write.descriptorType,data);
    }
}
void finalize(Descriptors &out,bool fresh_storage_output,uint64_t fresh_storage_serial=1) {
''' + tail + r'''
void reset() {
    slot.set_cache.clear();entries.clear();updates.clear();writes.clear();
    slot.recording=true;slot_serial=1;
    allocations=set_cache_experiment_skips=set_cache_fresh_skips=set_cache_hits=set_cache_misses=template_updates=plain_updates=0;
    api_descriptor_writes=0;
}
int main() {
    VkDescriptorBufferInfo buffer{handle<VkBuffer>(20),0,512};
    VkDescriptorImageInfo image{handle<VkSampler>(21),handle<VkImageView>(22),VK_IMAGE_LAYOUT_GENERAL};
    VkBufferView view=handle<VkBufferView>(23);
    for(bool honor:{false,true}) for(bool old:{false,true})
    for(bool fresh:{false,true}) for(bool fail:{false,true})
    for(bool experiment:{false,true}) for(bool matching_driver:{false,true}) {
        reset();honor_inline_enabled=honor;forced_old=old;fail_template=fail;
        honor_set_cache_bypass_experiment=experiment;
        properties.driverVersion=matching_driver?0x802fa02e:0x802fa02f;
        LayoutEntry layout;Descriptors out{&layout};
        for(unsigned i=0;i<3;++i) {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstBinding=i;write.descriptorCount=1;
            if(i==0) { write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer; }
            if(i==1) { write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image; }
            if(i==2) { write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;write.pTexelBufferView=&view; }
            writes.push_back(write);
        }
        for(unsigned call=0;call<2;++call) {
            buffer.offset=fresh?call*256:0;
            finalize(out,fresh);
            assert(out.set);
            {
                const Payload &payload=updates.back();
                assert(payload.buffer.buffer==buffer.buffer && payload.buffer.offset==buffer.offset && payload.buffer.range==512);
                assert(payload.image.sampler==image.sampler && payload.image.imageView==image.imageView && payload.image.imageLayout==image.imageLayout);
                assert(payload.view==view);
            }
        }
        const bool skip=honor && fresh && !old;
        assert(set_cache_fresh_skips==(skip?2u:0u));
        // The Honor bypass is not a separate switch any more: it fires whenever the descriptor memo
        // is on and the vendor is the Honor one, so the slot-local set cache stays unused there.
        // The fixture follows production instead of the older switch shape.
        const bool bypass=desc_memo_enabled && !writes.empty();
        assert(set_cache_experiment_skips==(!skip && bypass?2u:0u));
        assert(allocations==(fresh || bypass?2u:1u));
        assert(slot.set_cache.size()==(skip || bypass?0u:fresh?2u:1u));
        assert(set_cache_hits==(fresh || bypass?0u:1u));
        assert(set_cache_misses==(fresh || bypass?2u:1u));
        assert(updates.size()==allocations);
        assert((fail?plain_updates:template_updates)==allocations);
        assert(api_descriptor_writes==updates.size());
        // Slot retirement clears the old key before an arena offset is reused.
        slot.set_cache.clear();buffer.offset=0;finalize(out,fresh);
        assert(allocations==(fresh || bypass?3u:2u));
    }
    reset();honor_inline_enabled=true;forced_old=false;desc_memo_enabled=true;desc_memo_compute_enabled=true;
    honor_set_cache_bypass_experiment=true;properties.driverVersion=0x802fa02e;
    LayoutEntry empty;Descriptors out{&empty};
    finalize(out,false);finalize(out,false);
    assert(allocations==1 && set_cache_hits==1 && updates.empty());
    honor_set_cache_bypass_experiment=false;
    // A CPU synchronization during preparation can retire the allocation slot.
    for(bool still_recording:{false,true}) {
        reset();fail_template=false;slot.recording=still_recording;desc_memo_enabled=false;
        slot_serial=still_recording?2:1;
        LayoutEntry layout;Descriptors current{&layout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstBinding=0;write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer;
        writes.push_back(write);
        finalize(current,true,1);finalize(current,true,1);
        assert(set_cache_fresh_skips==0 && allocations==1 && set_cache_hits==1);
        assert(updates.size()==1 && updates[0].buffer.offset==buffer.offset);
    }
}
'''
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        if not ndk:
            ndk = sorted((Path.home() / 'Library/Android/sdk/ndk').glob('*'))[-1]
        headers = next((Path(ndk) / 'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include/vulkan'))
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            include = temporary / 'include'
            include.mkdir()
            (include / 'vulkan').symlink_to(headers, target_is_directory=True)
            (temporary / 'fixture.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-I', str(include),
                            str(temporary / 'fixture.cpp'), '-o', str(temporary / 'fixture')],
                           check=True, capture_output=True, text=True)
            return subprocess.run([str(temporary / 'fixture')], capture_output=True)

    def test_production_cache_and_update_paths(self):
        result = self.run_finalization()
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_old_cache_insertion_fails_fresh_output_contract(self):
        self.assertNotEqual(self.run_finalization(restore_old=True).returncode, 0)

    def test_allocation_fact_excludes_alias_only_writes(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('void descriptors_build(')
        end = source.index('// Render passes are cached by their full definition;', start)
        build = source[start:end]
        self.assertEqual(build.count('fresh_storage_output=true;'), 2)
        self.assertEqual(build.count('fresh_storage_serial=slot_serial;'), 2)
        alias_start = build.index('if(buf){')
        alias_end = build.index('static unsigned alias_hits', alias_start)
        self.assertNotIn('fresh_storage_output=true;', build[alias_start:alias_end])
        allocation_start = build.index('if(r.written){Buffer view,*b;')
        allocation_end = build.index('else{snap_site=3;', allocation_start)
        self.assertIn('readback_buffer(bytes', build[allocation_start:allocation_end])
        self.assertIn('fresh_storage_output=true;', build[allocation_start:allocation_end])


if __name__ == '__main__':
    unittest.main()
