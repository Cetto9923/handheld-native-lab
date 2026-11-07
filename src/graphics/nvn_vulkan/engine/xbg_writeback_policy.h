// PR15写回覆盖判定与图像CPU读取边界；基于pending_writebacks顺序及半开字节范围。
// PR15 writeback supersession and image CPU-read boundary, based on pending-writeback order and half-open byte ranges.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

inline bool xbg_wb_range_valid(uint64_t address,uint64_t bytes) {
    return bytes && bytes<=UINT64_MAX-address;
}
inline bool xbg_wb_contains(uint64_t outer,uint64_t outer_bytes,uint64_t inner,uint64_t inner_bytes) {
    return xbg_wb_range_valid(outer,outer_bytes) && xbg_wb_range_valid(inner,inner_bytes) &&
        inner>=outer && inner-outer<=outer_bytes && inner_bytes<=outer_bytes-(inner-outer);
}
inline bool xbg_wb_overlaps(uint64_t a,uint64_t a_bytes,uint64_t b,uint64_t b_bytes) {
    return xbg_wb_range_valid(a,a_bytes) && xbg_wb_range_valid(b,b_bytes) && a<b+b_bytes && b<a+a_bytes;
}
// pending_view chooses the newest overlapping writeback. If it only covers
// part of a requested storage texel, the host must merge on the CPU before
// acquiring a command slot or any resources that slot retirement can release.
template<class Pending>
bool xbg_wb_needs_partial_merge(const Pending &pending,uint64_t address,uint64_t bytes){
    for(auto it=pending.rbegin();it!=pending.rend();++it)
        if(xbg_wb_overlaps(address,bytes,it->addr,it->bytes))
            return !xbg_wb_contains(it->addr,it->bytes,address,bytes);
    return false;
}
// Plan oldest-to-newest overlays onto a CPU baseline. This only describes
// ranges; the caller must separately validate GPU buffer lifetime, barriers,
// pending reports, and VkBuffer copy alignment before recording any command.
struct XbgWritebackOverlay {size_t index;uint64_t source_offset,dest_offset,bytes;};
template<class Pending>
bool xbg_wb_overlay_plan(const Pending &pending,uint64_t address,uint64_t bytes,std::vector<XbgWritebackOverlay> &out){
    if(!xbg_wb_range_valid(address,bytes) || (bytes&3u))return false;
    std::vector<XbgWritebackOverlay> plan;
    for(size_t i=0;i<pending.size();++i){
        const auto &w=pending[i];if(!xbg_wb_range_valid(w.addr,w.bytes))return false;
        if(!xbg_wb_overlaps(address,bytes,w.addr,w.bytes))continue;
        const uint64_t begin=address>w.addr?address:w.addr;
        const uint64_t end=(address+bytes)<(w.addr+w.bytes)?address+bytes:w.addr+w.bytes;
        XbgWritebackOverlay copy{i,begin-w.addr,begin-address,end-begin};
        if((copy.source_offset|copy.dest_offset|copy.bytes)&3u)return false;
        plan.push_back(copy);
    }
    if(plan.empty())return false;out.swap(plan);return true;
}
template<class Pending,class Flags,class Aliases>
bool xbg_wb_superseded(const Pending &pending,size_t index,bool enabled,Flags flags,Aliases aliases) {
    if(!enabled || index>=pending.size())return false;
    const auto &old=pending[index];
    if(!xbg_wb_range_valid(old.addr,old.bytes) || old.bytes>SIZE_MAX)return false;
    bool newer=false;
    for(size_t i=index+1;i<pending.size();++i)if(xbg_wb_contains(pending[i].addr,pending[i].bytes,old.addr,old.bytes)) {newer=true;break;}
    if(!newer)return false;
    int pool_flags=flags(old.addr,old.bytes);
    return pool_flags>=0 && !(pool_flags&~0x3ff) && (pool_flags&7)==1 && !(pool_flags&8) && !aliases(old.addr,old.bytes);
}

// 先复制描述符，再同步CPU读取，最后重新查缓存；同步可能删除旧映射或释放其source描述符。
// Copy the descriptor before synchronizing CPU reads, then look up the cache anew; synchronization may erase old mappings or release their source descriptor.
template<class Texture,class Resolve,class Lookup>
decltype(auto) xbg_image_after_cpu_access(const Texture &texture,bool read,Resolve resolve,Lookup lookup) {
    Texture snapshot=texture;
    if(read && snapshot.mem && snapshot.L.size)resolve(uint64_t(uintptr_t(snapshot.mem)),uint64_t(snapshot.L.size));
    return lookup(snapshot);
}
