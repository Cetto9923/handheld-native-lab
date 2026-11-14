#pragma once

#include <cstdint>
#include <map>
#include <utility>

struct XbgTexelSnapshotScope {
    uint64_t slot, submission;
};

struct XbgTexelSnapshotSource {
    uint64_t buffer, offset, version;
    bool operator==(const XbgTexelSnapshotSource &other) const {
        return buffer==other.buffer && offset==other.offset && version==other.version;
    }
};

// Copies live in the recording slot's arena. A scope change forgets every
// copy before that arena can be reused. Source versions identify producers;
// overlapping in-place writes invalidate the corresponding guest ranges.
template<class Snapshot>
class XbgTexelSnapshotCache {
    struct Entry {
        XbgTexelSnapshotSource source;
        Snapshot copy;
    };
    std::map<std::pair<uint64_t,uint64_t>,Entry> entries;
    XbgTexelSnapshotScope scope{};

    static bool valid_range(uint64_t address,uint64_t bytes) {
        return bytes && address<=UINT64_MAX-(bytes-1);
    }
    void select_scope(XbgTexelSnapshotScope next) {
        if(scope.slot!=next.slot || scope.submission!=next.submission) {
            entries.clear();
            scope=next;
        }
    }

public:
    bool lookup(XbgTexelSnapshotScope next,uint64_t address,uint64_t bytes,
                XbgTexelSnapshotSource source,Snapshot &copy) {
        select_scope(next);
        if(!valid_range(address,bytes) || !valid_range(source.offset,bytes))return false;
        auto found=entries.find({address,bytes});
        if(found==entries.end() || !(found->second.source==source))return false;
        copy=found->second.copy;
        return true;
    }

    void store(XbgTexelSnapshotScope next,uint64_t address,uint64_t bytes,
               XbgTexelSnapshotSource source,const Snapshot &copy) {
        select_scope(next);
        if(!valid_range(address,bytes) || !valid_range(source.offset,bytes))return;
        // A bounded optimization. A full cache keeps the original copy path.
        auto key=std::make_pair(address,bytes);
        if(entries.size()<4096 || entries.find(key)!=entries.end())
            entries.insert_or_assign(key,Entry{source,copy});
    }

    void invalidate(uint64_t address,uint64_t bytes) {
        if(!bytes)return;
        if(!valid_range(address,bytes)) {
            entries.clear();
            return;
        }
        const uint64_t last=address+bytes-1;
        for(auto it=entries.begin();it!=entries.end();) {
            const uint64_t begin=it->first.first;
            const uint64_t end=begin+it->first.second-1;
            if(address<=end && begin<=last)it=entries.erase(it);
            else ++it;
        }
    }

    void reset() { entries.clear(); }
};
