/* The NVN window family of the FFRS RG477M native runtime.
 *
 * The guest creates a window that is bound to the platform window it got
 * from the nn::vi bridge, and it hands the window the textures it wants to
 * present into. That is the seam between the guest and the presentation
 * path: nvnWindowBuilderSetNativeWindow carries the platform window, and
 * nvnWindowInitialize is where this runtime turns it into a real surface and
 * a real swapchain.
 *
 * As with the queue family, the state lives on the host and is keyed by the
 * guest's addresses. Nothing is written into guest memory, and the guest's
 * texture handles are copied when the builder receives them, so no guest
 * pointer is dereferenced later.
 */
#ifndef FFRS_NVN_WINDOW_H
#define FFRS_NVN_WINDOW_H

#include <stddef.h>
#include <stdint.h>

/* How many texture handles a window builder keeps. The guest's swapchain
 * has a handful of images; anything beyond this is reported instead of
 * silently dropped. */
#define NVN_WINDOW_MAX_TEXTURES 8

typedef struct nvn_window_family_entry {
    const char *name;
    void *fn;
} nvn_window_family_entry;

size_t nvn_window_family_count(void);
const nvn_window_family_entry *nvn_window_family_entry_at(size_t index);

/* Statistics for the contract. */
typedef struct nvn_window_stats {
    uint64_t builder_calls;
    uint64_t initialized;
    uint64_t finalized;
    uint64_t refused;
    uint64_t surfaces_created;
    uint64_t swapchains_created;
    uint64_t textures_recorded;
    int live_windows;
    int surface_present;
    uint32_t width;
    uint32_t height;
} nvn_window_stats;

void nvn_window_get_stats(nvn_window_stats *out);

#endif /* FFRS_NVN_WINDOW_H */
