/* The symbols the ported Vulkan engine expects from its host and does not define itself.
 *
 * The engine (engine/) was written against a host that provided the NVN ABI, the dirty-page
 * tracker and the memory pool flags. That host is not part of this port: the FFRS runtime owns
 * the NVN entry table, and this file only carries the few symbols the engine links against
 * unconditionally.
 *
 * What is here:
 *   nvn_new_serial()             the global increasing sequence both the host and the engine use to
 *                                name the host textures and programs (nvn_obj.h: "New serial (> 0,
 *                                globally increasing)").
 *   nvn_dirty_since()            the dirty-page tracker's "was this range written since the stamp"
 *                                query. The engine declares it weak and only uses it together with
 *                                the rest of the tracker, which this port does not have.
 *   nvn_pool_flags()             the memory pool flags (NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS lets
 *                                the engine skip a storage writeback a later writeback already
 *                                covers). There is no pool flag registry here.
 *   xbg_record_command()         writes one cloned command record into the command buffer's current
 *                                memory segment; the engine calls it from xbg_copy_commands().
 *   xbg_host_command_callback()  the hook the engine calls when a cloned command buffer runs out of
 *                                space. The host installs its own function with
 *                                xbg_host_bridge_set_command_callback().
 *
 * What is NOT here: the NVN ABI itself (xbg_nvn_get_proc_address is declared by the engine but
 * never defined by it; the FFRS runtime supplies the entry table).
 */
#ifndef FFRS_NVN_VULKAN_HOST_BRIDGE_H
#define FFRS_NVN_VULKAN_HOST_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "nvn/nvn_obj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The hook the host installs for the "the command buffer is out of memory" event.
 *
 * It is called with the command buffer that ran out of space and the number of bytes the caller
 * needs. Returning zero means "not handled": the engine then takes its own path and calls the
 * guest's NVN command-memory callback. Returning non-zero means the host took care of it (it
 * added memory to the command buffer, for instance) and the engine does nothing more. */
typedef int (*xbg_command_callback_fn)(cmd_t *command, size_t bytes);

/* Install the hook (NULL removes it). */
void xbg_host_bridge_set_command_callback(xbg_command_callback_fn fn);

/* The hook that is installed, or NULL. */
xbg_command_callback_fn xbg_host_bridge_command_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* FFRS_NVN_VULKAN_HOST_BRIDGE_H */
