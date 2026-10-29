#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <vector>

// Metadata for shader 9bee7f09d6c655c3 only. Its validated integer module writes
// {template[2], GPU instance count, template[3], 0, 0} at template[0] uint words.
// This does not calculate, replace, or read back the instance count. Callers
// must prove exact shader/bindings, CPU-clean descriptor snapshots, and that
// other storage bindings do not alias the argument destination.
struct XbgIndirectTemplateRow {
    uint64_t address;
    uint32_t index_count,first_index;
};
inline bool xbg_indirect_templates(std::vector<XbgIndirectTemplateRow> &out,
    const std::vector<uint32_t> &words,const uint32_t params[4],
    uint64_t destination,uint64_t destination_bytes,uint32_t gx,uint32_t gy,uint32_t gz){
    out.clear();
    const uint64_t start=params[0],count=params[1];
    if(!destination || destination_bytes<20 || destination_bytes>65536 || (destination&3) ||
       (destination_bytes&3) || destination+destination_bytes<destination || !gx || gy!=1 || gz!=1 ||
       !count || count>256 || count>uint64_t(gx)*64 || words.size()>16384 ||
       start+count>words.size()/5)return false;
    std::vector<XbgIndirectTemplateRow> rows;rows.reserve(size_t(count));
    for(uint64_t j=0;j<count;j++){
        const uint32_t *q=words.data()+(start+j)*5;const uint64_t offset=uint64_t(q[0])*4;
        if(offset>destination_bytes-20 || q[2]>INT32_MAX || q[3]>INT32_MAX)return false;
        rows.push_back({destination+offset,q[2],q[3]});
    }
    std::sort(rows.begin(),rows.end(),[](const XbgIndirectTemplateRow &a,const XbgIndirectTemplateRow &b){return a.address<b.address;});
    for(size_t i=1;i<rows.size();i++)if(rows[i].address<rows[i-1].address+20)return false;
    out.swap(rows);return true;
}
