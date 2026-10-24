/* The NVN device layer of the FFRS RG477M native runtime.
 *
 * The guest is Unreal Engine's Switch NVNRHI. It obtains every NVN entry
 * point by name, at run time, and it never checks the pointer it gets back,
 * so this layer has to answer every name the guest asks for.
 *
 * The dispatch is two levels, which is what the guest code actually does
 * (see docs/analysis/nvn_device_requirements.md section 1):
 *
 *   1. nvnBootstrapLoader is asked for exactly two names:
 *      "nvnDeviceInitialize" and "nvnDeviceGetProcAddress".
 *   2. every other name is resolved through the nvnDeviceGetProcAddress that
 *      the bootstrap handed out, with the device as the first argument.
 *
 * The rules this layer follows:
 *
 *   - nvnDeviceInitialize reports success only when a real device was created.
 *     There is no "assume it worked" path: on a host build without a Vulkan
 *     implementation it reports failure, which is the truth there.
 *   - A name the guest asks for never resolves to NULL, because the guest
 *     stores the pointer and calls it without a NULL test, and a NULL there
 *     sends the guest to pc=0. Names outside the nvn namespace are refused,
 *     which is safe because the guest never asks for those.
 *   - An NVN entry point that this runtime does not implement yet resolves to
 *     an honest trap that names the missing API and stops the run, instead of
 *     a fake success or a silent no-op that would move the failure somewhere
 *     less legible.
 */
#ifndef FFRS_NVN_DEVICE_H
#define FFRS_NVN_DEVICE_H

#include <stddef.h>
#include <stdint.h>

/* The NVN device builder is the guest's own 64 byte stack area: the guest
 * reserves it, passes its address to nvnDeviceBuilderSetDefaults,
 * nvnDeviceBuilderSetFlags and nvnDeviceInitialize, and never reads or writes
 * a field of it. The layout below is therefore this runtime's own; it only
 * has to fit in those 64 bytes and to be consumed by the initialisation,
 * because the guest reuses the same stack area right afterwards. */
#define NVN_DEVICE_BUILDER_SIZE 64

/* Exit code for a guest call into an NVN entry point that is not implemented
 * yet. It is distinct from the guest's own fatal error (134) and from the
 * runtime's fault exit (128 + signal). */
#define NVN_UNIMPLEMENTED_EXIT_CODE 97

/* How many per-API traps this layer can hand out. The guest resolves 534
 * names, so this leaves room for every one of them. */
#define NVN_TRAP_SLOTS 640

/* An entry point that returns nothing leaves x0 at zero, so a caller that
 * inspects x0 after the call gets a defined value instead of a stale
 * pointer. The guest does not read x0 after these calls; the contract does,
 * and it wants a defined answer. */
#define NVN_VOID_RETURN() __asm__ volatile("mov x0, xzr")

typedef struct nvn_device_state nvn_device_state;

/* The platform specific half of the device. On Android this is a real Vulkan
 * device; on a host build without a Vulkan implementation it reports that
 * there is no device, which is the truth there. */
typedef struct nvn_backend {
    const char *name;
    /* Create the real device. Returns 1 when a real device exists and 0 on an
     * honest failure. This must never report success without a device. */
    int (*create)(nvn_device_state *device, const void *builder);
    /* Release what create() took. Safe to call without a device. */
    void (*destroy)(nvn_device_state *device);
    /* Write a factual description of the device into out[] for the log.
     * Never invents a name, a size or a capability. */
    void (*describe)(const nvn_device_state *device, char *out, size_t out_size);
    /* Hand out the platform queue. Returns 1 when a real queue exists and
     * writes its handle into out_queue; returns 0 when this backend has no
     * queue at all. A queue that pretends to exist is worse than no queue,
     * so a build without one says so. */
    int (*get_queue)(nvn_device_state *device, void **out_queue);
    /* Create the platform surface and the swapchain for a platform window.
     * The window is an opaque handle here: on Android it is an ANativeWindow,
     * and the backend that knows about that type does the conversion. The
     * size of the created presentation path is written back, because the
     * guest asks for it later. Returns 1 when both the surface and the
     * swapchain exist; returns 0 and reports why when they do not. */
    int (*create_window_surface)(nvn_device_state *device, void *native_window,
                                 uint32_t *out_width, uint32_t *out_height,
                                 void **out_surface, void **out_swapchain);
    /* Release what create_window_surface() took. Safe to call without one. */
    void (*destroy_window_surface)(nvn_device_state *device, void *surface, void *swapchain);
} nvn_backend;

struct nvn_device_state {
    const nvn_backend *backend;
    void *backend_state;          /* platform private */
    void *builder;                /* the builder address the guest passed last */
    uint32_t flags;               /* what the guest put into the builder */
    int initialized;              /* the guest's initialisation succeeded */
    int real_device;              /* a real device exists */
    void *guest_device;           /* the device pointer the guest passed in */
    uint64_t resolved;            /* names the guest resolved */
    uint64_t implemented_resolved;
    uint64_t trapped_resolved;
    uint64_t calls;               /* calls into implemented entry points */
    uint64_t unimplemented_calls;
    char description[192];
    /* The device state that only carries information. */
    int window_origin_mode;
    int depth_mode;
    int intermediate_shader_cache;
    int debug_callback_installed;
    void *debug_callback;
    void *debug_callback_userdata;
    uint32_t debug_domain_next;
    char debug_label[64];
};

/* The backend of this build. */
const nvn_backend *nvn_device_default_backend(void);
void nvn_device_set_backend(const nvn_backend *backend);
const nvn_backend *nvn_device_backend(void);

/* The device state of this process. The guest creates one device. */
nvn_device_state *nvn_device_state_handle(void);

/* True when the device is the one the guest initialized and a real device
 * exists behind it. The other object families ask this before they promise
 * anything, so that a queue is never created against a device that does not
 * exist. A NULL device pointer means "the device of this process". */
int nvn_device_is_ready(const void *device);

/* Level 1: the bootstrap. It answers exactly the two names the guest asks
 * for and refuses everything else. */
void *nvn_bootstrap_loader(const char *name);

/* Level 2: the device scoped lookup. It answers the rest of the nvn
 * namespace and refuses names outside it. */
void *nvn_device_get_proc_address(void *device, const char *name);

/* The implemented entry points. Their signatures are taken from the guest's
 * own call sites, not from a header:
 *
 *   nvnDeviceInitialize(device, builder) -> boolean in w0
 *       the guest reads the low byte of the result at 0x14354b4 and branches
 *       to its fatal path when it is zero.
 *   nvnDeviceBuilderSetDefaults(builder)
 *   nvnDeviceBuilderSetFlags(builder, flags)
 *       both are called with the builder address in x0 at 0x1435490 and
 *       0x14354a0; the flags arrive in w21.
 *   nvnDeviceGetInteger(device, info, out)
 *       the guest calls it at 0x14354e8 with (device, 0, sp) and at 0x14354fc
 *       with (device, 1, sp+0x5c), so it takes three arguments and writes the
 *       value through the third one. The two selectors it uses are the NVN
 *       API major and minor version.
 *   nvnDeviceGetCurrentTimestampInNanoseconds(device) -> uint64_t in x0
 *       the guest calls it at 0x1444b18, 0x1444b40, 0x1444b68 and 0x1444b90
 *       with only the device in x0 and reads the result from x0 right after
 *       the call, so it takes one argument and returns the timestamp.
 */
uint64_t nvn_device_initialize(void *device, void *builder);
void nvn_device_finalize(void *device);
void nvn_device_builder_set_defaults(void *builder);
void nvn_device_builder_set_flags(void *builder, int flags);
int nvn_device_builder_get_flags(const void *builder);
void nvn_device_get_integer(void *device, int info, int *out_value);
uint64_t nvn_device_get_current_timestamp_in_nanoseconds(void *device);

/* The rest of the device family. These are the entry points that only carry
 * state, so they can be answered truthfully without a graphics object:
 *
 *   nvnDeviceSetDebugLabel(device, label)              void
 *   nvnDeviceSetIntermediateShaderCache(device, on)    void
 *   nvnDeviceInstallDebugCallback(device, cb, ud, on)  void, the callback is
 *       stored and never called, because calling back into the guest from a
 *       host stub would need the guest thread and the guest TLS context
 *   nvnDeviceGenerateDebugDomainId(device, label)      -> an id
 *   nvnDeviceSetWindowOriginMode(device, mode)         void
 *   nvnDeviceGetWindowOriginMode(device)               -> the mode that was set
 *   nvnDeviceSetDepthMode(device, mode)                void
 *   nvnDeviceGetDepthMode(device)                      -> the mode that was set
 *   nvnDeviceRegisterFastClearColor(device, c, fmt)    -> false, this device
 *   nvnDeviceRegisterFastClearColori(device, c, fmt)      does not register
 *   nvnDeviceRegisterFastClearColorui(device, c, fmt)     fast clears
 *   nvnDeviceRegisterFastClearDepth(device, d)         -> false
 *   nvnDeviceIsExternalDebuggerAttached(device)        -> false
 *   nvnDeviceGetTimestampInNanoseconds(device, c)      -> the host clock
 *   nvnDeviceApplyDeferredFinalizes(device, n)         void
 *   nvnDeviceFinalizeCommandHandle(device, handle)     void
 *   nvnDeviceWalkDebugDatabase(device, ...)            void, the guest callback
 *       is never called
 */
void nvn_device_set_debug_label(void *device, const char *label);
void nvn_device_set_intermediate_shader_cache(void *device, int enabled);
void nvn_device_install_debug_callback(void *device, void *callback, void *userdata, int enabled);
uint32_t nvn_device_generate_debug_domain_id(void *device, const char *label);
void nvn_device_set_window_origin_mode(void *device, int mode);
int nvn_device_get_window_origin_mode(void *device);
void nvn_device_set_depth_mode(void *device, int mode);
int nvn_device_get_depth_mode(void *device);
int nvn_device_register_fast_clear_color(void *device, const float *color, int format);
int nvn_device_register_fast_clear_colori(void *device, const int *color, int format);
int nvn_device_register_fast_clear_colorui(void *device, const unsigned int *color, int format);
int nvn_device_register_fast_clear_depth(void *device, float depth);
int nvn_device_is_external_debugger_attached(void *device);
uint64_t nvn_device_get_timestamp_in_nanoseconds(void *device, const void *counter_data);
void nvn_device_apply_deferred_finalizes(void *device, int count);
void nvn_device_finalize_command_handle(void *device, uint64_t handle);
void nvn_device_walk_debug_database(void *device, int object_type, void *callback, void *userdata);

/* The trap that an unimplemented entry point resolves to. It is reached
 * through the per-API trampoline table, so it knows which API was called.
 * The second argument is where the trampoline left the guest's own argument
 * registers: the handler must read them there, because by the time it runs a
 * C function the argument registers already hold something else. */
void nvn_trampoline_handler(uint64_t index, const uint64_t *args);

/* Statistics for the contract. */
typedef struct nvn_device_stats {
    uint64_t resolved;
    uint64_t implemented_resolved;
    uint64_t trapped_resolved;
    uint64_t calls;
    uint64_t unimplemented_calls;
    int initialized;
    int real_device;
    uint32_t flags;
    uint64_t trap_slots_used;
    char description[192];
} nvn_device_stats;

void nvn_device_get_stats(nvn_device_stats *out);
const char *nvn_device_trap_name(uint64_t index);

#endif /* FFRS_NVN_DEVICE_H */
