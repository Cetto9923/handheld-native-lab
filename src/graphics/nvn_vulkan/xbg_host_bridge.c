/* The symbols the ported Vulkan engine expects from its host and does not define itself.
 * See xbg_host_bridge.h for what each one is and why it is here. */

#include "engine/xbg_game_internal.h"

#include "xbg_host_bridge.h"

#include <string.h>

/* The global increasing sequence the host and the engine share to name the host textures and the
 * host programs (nvn_obj.h: "New serial (> 0, globally increasing)"). The engine calls it when it
 * registers a texture by value; the FFRS runtime calls it when it creates a texture object. The
 * counter is shared so that the two never hand out the same number. */
static uint64_t g_serial;

uint64_t nvn_new_serial(void) { return __atomic_add_fetch(&g_serial, 1, __ATOMIC_RELAXED) + 1; }

/* The dirty-page tracker's "was this range written since the stamp" query.
 *
 * The engine declares this weak and uses it only together with the rest of the tracker
 * (nvn_dirty_tracked, nvn_dirty_protect, nvn_dirty_now), which this port does not have: the guest
 * pages are not write-protected here, so nothing can report a write. The stub answers "never
 * written", which keeps the cross-frame vertex / index cache and the texture dirty checks off. */
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) {
    (void)addr;
    (void)len;
    (void)stamp;
    return 0;
}

/* The memory pool flags of a range.
 *
 * The engine uses NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS to skip a storage writeback that a later
 * writeback already covers; without a flag registry no range carries that flag, so no writeback is
 * ever skipped. The stub answers "no flags". */
int nvn_pool_flags(const void *addr, size_t len) {
    (void)addr;
    (void)len;
    return 0;
}

/* Write one cloned command record into the command buffer's current memory segment.
 *
 * The engine's xbg_copy_commands() flattens a chain of recorded segments into a snapshot and then
 * writes it back record by record with this function (engine/xbg_game.cpp). The command buffer
 * gives its memory to the game, which may run out of it; the guest is then asked for more through
 * the NVN command-memory callback, and the record is written into the memory that comes back.
 *
 * A record that does not fit after two rounds of asking is a named failure and not a write past
 * the end of the segment: xbg_fatalf() goes through the host's fatal handler, which does not
 * return into the guest. */
void xbg_record_command(cmd_t *c, const void *record, uint32_t bytes) {
    if (!c || !c->recording || !record || bytes < 8 || (bytes & 7))
        xbg_fatalf("Invalid cloned command record of %u bytes", bytes);
    for (int tries = 0; !c->cur || c->cur + bytes + 24 > c->end; tries++) {
        if (!c->cb || tries == 2) xbg_fatalf("Command memory exhausted while copying a command record");
        xbg_invoke_command_callback(c, bytes + 24);
    }
    memcpy(c->cur, record, bytes);
    c->cur += bytes;
}

/* The host's hook for the "the command buffer is out of memory" event. */
static xbg_command_callback_fn g_command_callback;

void xbg_host_bridge_set_command_callback(xbg_command_callback_fn fn) { g_command_callback = fn; }

xbg_command_callback_fn xbg_host_bridge_command_callback(void) { return g_command_callback; }

/* The engine's weak entry point (engine/xbg_game.cpp, xbg_invoke_command_callback): the host's
 * function pointer goes first, and the engine falls back to the guest's NVN command-memory
 * callback when there is none. */
int xbg_host_command_callback(cmd_t *c, size_t n) {
    xbg_command_callback_fn fn = g_command_callback;
    return fn ? fn(c, n) : 0;
}
