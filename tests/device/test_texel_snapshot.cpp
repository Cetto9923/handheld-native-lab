#include "../../src/graphics/nvn_vulkan/engine/xbg_texel_snapshot.h"

#include <cassert>
#include <cstdio>

struct Snapshot {
    uint64_t buffer, offset;
};

int main() {
    XbgTexelSnapshotCache<Snapshot> cache;
    XbgTexelSnapshotScope scope{5,12};
    XbgTexelSnapshotSource source{100,256,7};
    Snapshot copy{200,512}, found{};
    cache.store(scope,0x1000,256,source,copy);
    assert(cache.lookup(scope,0x1000,256,source,found));
    assert(found.buffer==200 && found.offset==512);

    cache.invalidate(0x2000,128); // Unrelated writes retain the copy.
    assert(cache.lookup(scope,0x1000,256,source,found));
    cache.invalidate(0x1100,128); // Adjacent ranges do not overlap.
    assert(cache.lookup(scope,0x1000,256,source,found));
    cache.invalidate(0x1080,1);
    assert(!cache.lookup(scope,0x1000,256,source,found));
    cache.store(scope,0x1000,256,source,copy);
    cache.store(scope,0x1080,128,{100,384,7},copy);
    cache.store(scope,0x3000,256,source,copy);
    cache.invalidate(0x1090,16);
    assert(!cache.lookup(scope,0x1000,256,source,found));
    assert(!cache.lookup(scope,0x1080,128,{100,384,7},found));
    assert(cache.lookup(scope,0x3000,256,source,found));
    cache.reset();

    cache.store(scope,0x1000,256,source,copy);
    assert(!cache.lookup(scope,0x1000,256,{101,256,7},found));
    assert(!cache.lookup(scope,0x1000,256,{100,260,7},found));
    assert(!cache.lookup(scope,0x1000,256,{100,256,8},found));
    assert(!cache.lookup(scope,0x1000,128,source,found));
    assert(!cache.lookup(scope,0x1004,256,source,found));
    cache.store(scope,0x1000,256,{101,256,8},{201,768});
    assert(cache.lookup(scope,0x1000,256,{101,256,8},found));
    assert(found.buffer==201 && found.offset==768);
    assert(!cache.lookup(scope,0x1000,256,source,found));

    cache.store(scope,0x1000,256,source,copy);
    assert(!cache.lookup({6,12},0x1000,256,source,found));
    assert(!cache.lookup(scope,0x1000,256,source,found));
    cache.store(scope,0x1000,256,source,copy);
    assert(!cache.lookup({5,13},0x1000,256,source,found));
    cache.store(scope,0x1000,256,source,copy);
    cache.reset();
    assert(!cache.lookup(scope,0x1000,256,source,found));

    cache.store(scope,UINT64_MAX,1,source,copy);
    cache.invalidate(UINT64_MAX-1,1);
    assert(cache.lookup(scope,UINT64_MAX,1,source,found));
    cache.invalidate(UINT64_MAX,1);
    assert(!cache.lookup(scope,UINT64_MAX,1,source,found));
    cache.store(scope,UINT64_MAX,2,source,copy);
    assert(!cache.lookup(scope,UINT64_MAX,2,source,found));
    cache.store(scope,0x1000,256,source,copy);
    cache.invalidate(UINT64_MAX,2); // An invalidation overflow is conservative.
    assert(!cache.lookup(scope,0x1000,256,source,found));
    cache.store(scope,0x1000,0,source,copy);
    assert(!cache.lookup(scope,0x1000,0,source,found));
    cache.store(scope,0x1000,256,{100,UINT64_MAX,7},copy);
    assert(!cache.lookup(scope,0x1000,256,{100,UINT64_MAX,7},found));

    for(uint64_t i=0;i<4096;++i)cache.store(scope,0x10000+i*256,256,source,copy);
    cache.store(scope,0x200000,256,source,copy);
    assert(!cache.lookup(scope,0x200000,256,source,found));
    cache.store(scope,0x10000,256,source,{203,1280});
    assert(cache.lookup(scope,0x10000,256,source,found));
    assert(found.buffer==203 && found.offset==1280);
    cache.reset();

    // The off arm may detect a match but must still copy and replace it.
    cache.store(scope,0x1000,256,source,copy);
    const bool enabled=false;
    const bool would_hit=cache.lookup(scope,0x1000,256,source,found);
    assert(would_hit && !(enabled && would_hit));
    cache.store(scope,0x1000,256,source,{202,1024});
    assert(cache.lookup(scope,0x1000,256,source,found));
    assert(found.buffer==202 && found.offset==1024);
    std::puts("texel snapshot scope/source/range invalidation PASS");
}
