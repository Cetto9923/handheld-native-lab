/* The seam between the NVN device layer and the Vulkan engine that replays
 * the commands.
 *
 * The device layer records what the guest asks for; the engine turns those
 * commands into real Vulkan work and shows the game's own render target.
 * This file is where the two meet: it gives the engine the device and the
 * queue of this runtime, hands it the commands the recorder made, and gives
 * it the objects the guest registered.
 *
 * One device, one queue. The engine renders into images of its own and the
 * present path copies the game's image into the swapchain, so that the
 * picture on the screen is the picture the guest's commands produced and not
 * a picture of the host's own making.
 */
#ifndef FFRS_NVN_ENGINE_H
#define FFRS_NVN_ENGINE_H

#include <stddef.h>
#include <stdint.h>

typedef struct nvn_device_state nvn_device_state;

#ifdef __cplusplus
extern "C" {
#endif

/* Where the genuine shader bundles are.
 *
 * The engine loads a bundle by the name of the shader it holds, and the name
 * is the hash of the control section of the guest's shader. The directory
 * has to hold the bundles that were exported from the game's own shaders. */
void nvn_engine_set_shader_directory(const char *directory);

/* Start the engine on the device of this runtime. Returns 1 when the engine
 * is running and 0 when it is not, with the reason written into reason. */
int nvn_engine_initialize(nvn_device_state *device, char *reason, size_t reason_size);

/* Whether the engine is running. */
int nvn_engine_ready(void);

/* The engine's submitted-work fence and real Vulkan completion wait. */
uint64_t nvn_engine_last_fence(void);
int nvn_engine_wait(uint64_t fence, uint64_t timeout);

/* Switch the engine off.
 *
 * This is a diagnostic switch and not a mode of operation: a runtime without
 * the engine records the guest's commands and reads them, and shows nothing.
 * It exists so that the cause of a stall can be told apart from the engine
 * without a rebuild. */
void nvn_engine_set_enabled(int enabled);

/* Give the engine the commands of one submission.
 *
 * The handles are the addresses of the first record of each package, in the
 * order the guest gave them. */
int nvn_engine_submit(const uint64_t *handles, int count);

/* Tell the engine about a memory pool of the guest, so that the addresses
 * the guest uses can be resolved. */
void nvn_engine_pool_add(void *memory, size_t bytes);
void nvn_engine_pool_del(void *memory);

/* Give the engine a texture or a sampler that the guest registered in a
 * descriptor pool. */
int nvn_engine_register_texture(const void *guest_pool, int id, const void *guest_texture,
                                 const void *guest_view);
int nvn_engine_register_sampler(const void *guest_pool, int id, const void *guest_sampler);

/* Show the image of the guest's swapchain texture. */
int nvn_engine_present(const void *guest_window, int index);

/* What the engine has done, for the log. */
typedef struct nvn_engine_stats {
    uint64_t submissions;
    uint64_t draws;
    uint64_t dispatches;
    uint64_t presents;
    uint64_t shader_loads;
} nvn_engine_stats;

void nvn_engine_get_stats(nvn_engine_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* FFRS_NVN_ENGINE_H */
