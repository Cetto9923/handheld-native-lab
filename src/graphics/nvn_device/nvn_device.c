/* The NVN device layer implementation.
 *
 * See nvn_device.h for what this layer guarantees and why.
 *
 * The one thing that needs explaining here is the per-API trap table. The
 * guest resolves every NVN entry point by name and never checks the pointer
 * it gets back, so a name that this runtime does not implement yet cannot
 * resolve to NULL (that would send the guest to pc=0) and must not resolve
 * to a fake success either. It resolves to a trap instead: a small piece of
 * code that knows which API was called, reports it, and ends the run.
 *
 * The table is eight bytes per API, built by the assembler below, with the
 * API index baked into the instruction stream. It is ordinary static code,
 * not writable memory and not generated at run time, so it needs no
 * executable mapping tricks and it behaves the same on the host and on the
 * device. The block branches to the handler with a branch rather than a
 * call, so x30 still holds the guest's return address when the handler
 * starts, which is what lets the report name the call site.
 */
#include "nvn_device.h"
#include "nvn_blend_state.h"
#include "nvn_memory_pool.h"
#include "nvn_queue.h"
#include "nvn_program.h"
#include "nvn_render_state.h"
#include "nvn_sampler_pool.h"
#include "nvn_texture_pool.h"
#include "nvn_window.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* The per-API trap table                                              */
/* ------------------------------------------------------------------ */

#if defined(__aarch64__)
#if defined(__APPLE__)
#define NVN_ASM_TABLE "_nvn_trampoline_table"
#define NVN_ASM_HANDLER "_nvn_trampoline_handler"
#else
#define NVN_ASM_TABLE "nvn_trampoline_table"
#define NVN_ASM_HANDLER "nvn_trampoline_handler"
#endif

/* The arguments the guest passed to a trapped entry point.
 *
 * The trampoline puts them on the stack before it touches any register. The
 * handler that prints them is a C function, and its very first call already
 * overwrites the argument registers, so reading them inside the handler would
 * report the leftovers of the print instead of the guest's own values. The
 * first argument is the object the call is about, which is the one that tells
 * two calls of the same shape apart. */
__asm__(
    ".text\n"
    ".align 3\n"
    ".global " NVN_ASM_TABLE "\n"
    NVN_ASM_TABLE ":\n"
    ".macro NVN_TRAMP idx\n"
    "  sub sp, sp, #64\n"
    "  stp x0, x1, [sp, #0]\n"
    "  stp x2, x3, [sp, #16]\n"
    "  stp x4, x5, [sp, #32]\n"
    "  stp x6, x7, [sp, #48]\n"
    "  mov w0, #\\idx\n"
    "  add x1, sp, #0\n"
    "  b " NVN_ASM_HANDLER "\n"
    ".endm\n"
    ".set nvn_trampoline_i, 0\n"
    ".rept 640\n"
    "  NVN_TRAMP nvn_trampoline_i\n"
    "  .set nvn_trampoline_i, nvn_trampoline_i + 1\n"
    ".endr\n");

#define NVN_TRAP_TABLE_PRESENT 1
extern const char nvn_trampoline_table[];
#define NVN_TRAP_STRIDE 32
#else
/* Without the table there is no way to tell the traps apart, so every
 * unimplemented name shares one trap that reports the slot count instead of
 * the name. The build records which of the two this is. */
#define NVN_TRAP_TABLE_PRESENT 0
#define NVN_TRAP_STRIDE 0
#endif

static char s_trap_names[NVN_TRAP_SLOTS][96];
static uint64_t s_trap_used;

/* The device state. There is one device per process, which is what the guest
 * creates. */
static nvn_device_state s_device;

/* ------------------------------------------------------------------ */
/* The backend                                                         */
/* ------------------------------------------------------------------ */

static const nvn_backend *s_backend;

const nvn_backend *nvn_device_default_backend(void);

void nvn_device_set_backend(const nvn_backend *backend) { s_backend = backend; }

const nvn_backend *nvn_device_backend(void) {
    if (!s_backend) s_backend = nvn_device_default_backend();
    s_device.backend = s_backend;
    return s_backend;
}

nvn_device_state *nvn_device_state_handle(void) { return &s_device; }

int nvn_device_is_ready(const void *device) {
    if (!s_device.initialized || !s_device.real_device) return 0;
    /* The guest passes its own device pointer, never NULL. A NULL pointer is
     * therefore not a device, and saying so keeps "no device in the builder"
     * distinguishable from "the device of this process". */
    if (!device) return 0;
    return device == s_device.guest_device;
}

/* ------------------------------------------------------------------ */
/* The builder                                                         */
/* ------------------------------------------------------------------ */

/* Our own builder layout. The guest never reads or writes these fields; it
 * only passes the address to the three builder functions and reuses the same
 * stack area for other values right after the device is initialized. So the
 * layout is this runtime's own, it only has to fit in the 64 bytes the guest
 * reserved and to be consumed by the initialisation. */
#define NVN_BUILDER_MAGIC 0x4e564e4445563031ULL /* "NVNDEV01" */

typedef struct {
    uint64_t magic;
    uint64_t flags;
} nvn_builder_layout;

static uint64_t nvn_builder_read_flags(const void *builder) {
    const nvn_builder_layout *b = (const nvn_builder_layout *)builder;
    if (!b || b->magic != NVN_BUILDER_MAGIC) return 0;
    return b->flags;
}

/* An entry point that returns nothing leaves x0 at zero, so a caller that
 * inspects x0 after the call gets a defined value instead of a stale
 * pointer. The guest does not read x0 after these calls; the contract does,
 * and it wants a defined answer. The macro lives in nvn_device.h, because the
 * other object families need it too. */

void nvn_device_builder_set_defaults(void *builder) {
    nvn_builder_layout *b = (nvn_builder_layout *)builder;
    s_device.calls++;
    if (b) {
        b->magic = NVN_BUILDER_MAGIC;
        b->flags = 0;
    }
    NVN_VOID_RETURN();
}

void nvn_device_builder_set_flags(void *builder, int flags) {
    nvn_builder_layout *b = (nvn_builder_layout *)builder;
    s_device.calls++;
    if (b) {
        b->magic = NVN_BUILDER_MAGIC;
        b->flags = (uint64_t)(uint32_t)flags;
    }
    NVN_VOID_RETURN();
}

int nvn_device_builder_get_flags(const void *builder) {
    s_device.calls++;
    if (!builder) return 0;
    return (int)(uint32_t)nvn_builder_read_flags(builder);
}

/* ------------------------------------------------------------------ */
/* The implemented entry points                                        */
/* ------------------------------------------------------------------ */

uint64_t nvn_device_initialize(void *device, void *builder) {
    const nvn_backend *backend = nvn_device_backend();
    s_device.calls++;
    if (s_device.initialized) {
        fprintf(stderr, "[nvn] nvnDeviceInitialize called again; reporting the device that already exists\n");
        return s_device.real_device ? 1 : 0;
    }
    /* The builder has to be consumed here: the guest reuses that stack area
     * for its own values right after this call. */
    s_device.flags = (uint32_t)nvn_builder_read_flags(builder);
    s_device.builder = builder;
    s_device.guest_device = device;
    s_device.initialized = 1;
    if (backend && backend->create) {
        s_device.real_device = backend->create(&s_device, builder) ? 1 : 0;
        if (backend->describe) backend->describe(&s_device, s_device.description, sizeof(s_device.description));
    }
    fprintf(stderr, "[nvn] nvnDeviceInitialize: device=%p builder=%p flags=0x%" PRIx32 " -> %s\n",
            device, builder, s_device.flags, s_device.real_device ? "true" : "false");
    if (s_device.real_device) fprintf(stderr, "[nvn] device: %s\n", s_device.description);
    return s_device.real_device ? 1 : 0;
}

void nvn_device_finalize(void *device) {
    const nvn_backend *backend = nvn_device_backend();
    s_device.calls++;
    if (s_device.real_device && backend && backend->destroy) backend->destroy(&s_device);
    s_device.real_device = 0;
    fprintf(stderr, "[nvn] nvnDeviceFinalize: device=%p\n", device);
    NVN_VOID_RETURN();
}

/* nvnDeviceGetInteger(device, info, out).
 *
 * The guest calls this twice right after the device is initialized, at
 * 0x14354e8 with (device, 0, sp) and at 0x14354fc with (device, 1, sp+0x5c),
 * so it takes three arguments and writes the value through the third one.
 * The two selectors it uses are the NVN API major and minor version, so the
 * guest is asking this device which generation of the NVN API it speaks.
 *
 * The answer is the version of the specification this device layer
 * implements, which is the one the vendored 53.105 header documents. It is
 * not a claim that every entry point of that version is implemented: an entry
 * point that is missing stops the run and names itself, so an incomplete
 * device can never be mistaken for a complete one. */
#define NVN_DEVICE_API_MAJOR_VERSION 53
#define NVN_DEVICE_API_MINOR_VERSION 105

void nvn_device_get_integer(void *device, int info, int *out_value) {
    s_device.calls++;
    (void)device;
    if (out_value) {
        if (info == 0) *out_value = NVN_DEVICE_API_MAJOR_VERSION;
        else if (info == 1) *out_value = NVN_DEVICE_API_MINOR_VERSION;
        else {
            *out_value = 0;
            fprintf(stderr, "[nvn] nvnDeviceGetInteger: unknown selector %d -> 0\n", info);
        }
    }
    NVN_VOID_RETURN();
}

/* nvnDeviceGetCurrentTimestampInNanoseconds(device).
 *
 * The guest calls this at 0x1444b18, 0x1444b40, 0x1444b68 and 0x1444b90 with
 * only the device in x0, and reads the result from x0 right after the call,
 * so it takes one argument and returns the timestamp.
 *
 * The timestamp is the host's CLOCK_MONOTONIC. The guest uses it to measure
 * intervals, so one host clock is the honest answer; the runtime does not
 * claim to have a GPU clock. */
uint64_t nvn_device_get_current_timestamp_in_nanoseconds(void *device) {
    struct timespec ts;
    uint64_t ns = 0;
    s_device.calls++;
    (void)device;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns;
}

/* ------------------------------------------------------------------ */
/* The rest of the device family                                       */
/* ------------------------------------------------------------------ */

/* These entry points only carry state, so they can be answered truthfully
 * without a graphics object. Two rules apply:
 *
 *   - a callback the guest hands over is stored and never called, because
 *     calling back into the guest from a host stub needs the guest thread
 *     and the guest TLS context, and doing it wrong corrupts the guest;
 *   - a query this device cannot really answer reports failure rather than a
 *     success it has not got. A fast clear that is not registered is
 *     reported as not registered, so the guest falls back to a normal clear
 *     instead of believing an optimisation happened.
 */

void nvn_device_set_debug_label(void *device, const char *label) {
    s_device.calls++;
    (void)device;
    snprintf(s_device.debug_label, sizeof(s_device.debug_label), "%s", label ? label : "");
    fprintf(stderr, "[nvn] nvnDeviceSetDebugLabel: \"%s\"\n", s_device.debug_label);
    NVN_VOID_RETURN();
}

void nvn_device_set_intermediate_shader_cache(void *device, int enabled) {
    s_device.calls++;
    (void)device;
    s_device.intermediate_shader_cache = enabled;
    NVN_VOID_RETURN();
}

void nvn_device_install_debug_callback(void *device, void *callback, void *userdata, int enabled) {
    s_device.calls++;
    (void)device;
    s_device.debug_callback = callback;
    s_device.debug_callback_userdata = userdata;
    s_device.debug_callback_installed = enabled ? 1 : 0;
    fprintf(stderr, "[nvn] nvnDeviceInstallDebugCallback: callback=%p userdata=%p enabled=%d "
                    "(the callback is stored and never called)\n",
            callback, userdata, enabled ? 1 : 0);
    NVN_VOID_RETURN();
}

uint32_t nvn_device_generate_debug_domain_id(void *device, const char *label) {
    s_device.calls++;
    (void)device;
    (void)label;
    /* A domain id this runtime hands out. It identifies the debug objects
     * this device creates, and nothing else. */
    return ++s_device.debug_domain_next;
}

void nvn_device_set_window_origin_mode(void *device, int mode) {
    s_device.calls++;
    (void)device;
    s_device.window_origin_mode = mode;
    NVN_VOID_RETURN();
}

int nvn_device_get_window_origin_mode(void *device) {
    s_device.calls++;
    (void)device;
    return s_device.window_origin_mode;
}

void nvn_device_set_depth_mode(void *device, int mode) {
    s_device.calls++;
    (void)device;
    s_device.depth_mode = mode;
    NVN_VOID_RETURN();
}

int nvn_device_get_depth_mode(void *device) {
    s_device.calls++;
    (void)device;
    return s_device.depth_mode;
}

int nvn_device_register_fast_clear_color(void *device, const float *color, int format) {
    s_device.calls++;
    (void)device;
    (void)color;
    (void)format;
    /* This device does not register fast clears, so it says so. */
    return 0;
}

int nvn_device_register_fast_clear_colori(void *device, const int *color, int format) {
    s_device.calls++;
    (void)device;
    (void)color;
    (void)format;
    return 0;
}

int nvn_device_register_fast_clear_colorui(void *device, const unsigned int *color, int format) {
    s_device.calls++;
    (void)device;
    (void)color;
    (void)format;
    return 0;
}

int nvn_device_register_fast_clear_depth(void *device, float depth) {
    s_device.calls++;
    (void)device;
    (void)depth;
    return 0;
}

int nvn_device_is_external_debugger_attached(void *device) {
    s_device.calls++;
    (void)device;
    /* Nothing is attached to this device. */
    return 0;
}

uint64_t nvn_device_get_timestamp_in_nanoseconds(void *device, const void *counter_data) {
    struct timespec ts;
    uint64_t ns = 0;
    s_device.calls++;
    (void)device;
    (void)counter_data;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns;
}

void nvn_device_apply_deferred_finalizes(void *device, int count) {
    s_device.calls++;
    (void)device;
    (void)count;
    NVN_VOID_RETURN();
}

void nvn_device_finalize_command_handle(void *device, uint64_t handle) {
    s_device.calls++;
    (void)device;
    (void)handle;
    NVN_VOID_RETURN();
}

void nvn_device_walk_debug_database(void *device, int object_type, void *callback, void *userdata) {
    s_device.calls++;
    (void)device;
    (void)object_type;
    (void)callback;
    (void)userdata;
    /* The guest callback is not called: it would need the guest thread and
     * the guest TLS context, and this runtime does not enter the guest from
     * a host stub. */
    fprintf(stderr, "[nvn] nvnDeviceWalkDebugDatabase: the guest callback was not called\n");
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The trap                                                            */
/* ------------------------------------------------------------------ */

const char *nvn_device_trap_name(uint64_t index) {
    if (index >= NVN_TRAP_SLOTS) return "<unknown>";
    return s_trap_names[index];
}

void nvn_trampoline_handler(uint64_t index, const uint64_t *args) {
    uint64_t caller_lr;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    /* x30 still holds the guest's return address: the trampoline branches
     * here instead of calling, so nothing has replaced it yet. */
    __asm__ volatile("mov %0, x30" : "=r"(caller_lr));
    /* The arguments the guest really passed. The trampoline put them on the
     * stack before it overwrote any register, so they are still the guest's
     * own values here.
     *
     * The entry point the guest wanted is only known by name here, so the
     * signature of the call is not known either. Printing the arguments is
     * what makes the call site legible without guessing: the shape of the
     * call can be read off the log. The first argument is the object the
     * call is about, which is the one that identifies which of several same
     * shaped calls this is. */
    if (args) {
        a0 = args[0];
        a1 = args[1];
        a2 = args[2];
        a3 = args[3];
        a4 = args[4];
        a5 = args[5];
        a6 = args[6];
        a7 = args[7];
    }
    s_device.unimplemented_calls++;
    fprintf(stderr, "[nvn] unimplemented NVN entry \"%s\" was called (trap slot %" PRIu64 ", caller lr=0x%" PRIx64 ")\n",
            nvn_device_trap_name(index), index, caller_lr);
    fprintf(stderr, "[nvn]   args x0=0x%" PRIx64 " x1=0x%" PRIx64 " x2=0x%" PRIx64 " x3=0x%" PRIx64
                    " x4=0x%" PRIx64 " x5=0x%" PRIx64 " x6=0x%" PRIx64 " x7=0x%" PRIx64 "\n",
            a0, a1, a2, a3, a4, a5, a6, a7);
    fprintf(stderr, "[nvn] this runtime has no implementation for that API yet; stopping the run here "
                    "instead of pretending the call succeeded\n");
    fflush(NULL);
    _exit(NVN_UNIMPLEMENTED_EXIT_CODE);
}

/* The shared trap for a build without the per-API table. */
static void nvn_shared_trap(void) {
    uint64_t caller_lr;
    __asm__ volatile("mov %0, x30" : "=r"(caller_lr));
    s_device.unimplemented_calls++;
    fprintf(stderr, "[nvn] an unimplemented NVN entry was called (caller lr=0x%" PRIx64 ", %" PRIu64
                    " names resolved); this build has no per-API trap table\n",
            caller_lr, s_device.resolved);
    fflush(NULL);
    _exit(NVN_UNIMPLEMENTED_EXIT_CODE);
}

static uint64_t nvn_trap_allocate(const char *name) {
    uint64_t i;
    for (i = 0; i < s_trap_used; i++)
        if (!strcmp(s_trap_names[i], name)) return i;
    if (s_trap_used >= NVN_TRAP_SLOTS) return UINT64_MAX;
    i = s_trap_used++;
    snprintf(s_trap_names[i], sizeof(s_trap_names[i]), "%s", name);
    return i;
}

/* ------------------------------------------------------------------ */
/* The two level dispatch                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    void *fn;
} nvn_implemented_entry;

static const nvn_implemented_entry s_implemented[] = {
    { "nvnDeviceInitialize", (void *)(uintptr_t)nvn_device_initialize },
    { "nvnDeviceGetProcAddress", (void *)(uintptr_t)nvn_device_get_proc_address },
    { "nvnDeviceFinalize", (void *)(uintptr_t)nvn_device_finalize },
    { "nvnDeviceBuilderSetDefaults", (void *)(uintptr_t)nvn_device_builder_set_defaults },
    { "nvnDeviceBuilderSetFlags", (void *)(uintptr_t)nvn_device_builder_set_flags },
    { "nvnDeviceBuilderGetFlags", (void *)(uintptr_t)nvn_device_builder_get_flags },
    { "nvnDeviceGetInteger", (void *)(uintptr_t)nvn_device_get_integer },
    { "nvnDeviceGetCurrentTimestampInNanoseconds",
      (void *)(uintptr_t)nvn_device_get_current_timestamp_in_nanoseconds },
    { "nvnDeviceSetDebugLabel", (void *)(uintptr_t)nvn_device_set_debug_label },
    { "nvnDeviceSetIntermediateShaderCache",
      (void *)(uintptr_t)nvn_device_set_intermediate_shader_cache },
    { "nvnDeviceInstallDebugCallback", (void *)(uintptr_t)nvn_device_install_debug_callback },
    { "nvnDeviceGenerateDebugDomainId", (void *)(uintptr_t)nvn_device_generate_debug_domain_id },
    { "nvnDeviceSetWindowOriginMode", (void *)(uintptr_t)nvn_device_set_window_origin_mode },
    { "nvnDeviceGetWindowOriginMode", (void *)(uintptr_t)nvn_device_get_window_origin_mode },
    { "nvnDeviceSetDepthMode", (void *)(uintptr_t)nvn_device_set_depth_mode },
    { "nvnDeviceGetDepthMode", (void *)(uintptr_t)nvn_device_get_depth_mode },
    { "nvnDeviceRegisterFastClearColor", (void *)(uintptr_t)nvn_device_register_fast_clear_color },
    { "nvnDeviceRegisterFastClearColori", (void *)(uintptr_t)nvn_device_register_fast_clear_colori },
    { "nvnDeviceRegisterFastClearColorui", (void *)(uintptr_t)nvn_device_register_fast_clear_colorui },
    { "nvnDeviceRegisterFastClearDepth", (void *)(uintptr_t)nvn_device_register_fast_clear_depth },
    { "nvnDeviceIsExternalDebuggerAttached",
      (void *)(uintptr_t)nvn_device_is_external_debugger_attached },
    { "nvnDeviceGetTimestampInNanoseconds",
      (void *)(uintptr_t)nvn_device_get_timestamp_in_nanoseconds },
    { "nvnDeviceApplyDeferredFinalizes", (void *)(uintptr_t)nvn_device_apply_deferred_finalizes },
    { "nvnDeviceFinalizeCommandHandle", (void *)(uintptr_t)nvn_device_finalize_command_handle },
    { "nvnDeviceWalkDebugDatabase", (void *)(uintptr_t)nvn_device_walk_debug_database },
};

static const size_t s_implemented_count = sizeof(s_implemented) / sizeof(s_implemented[0]);

void *nvn_bootstrap_loader(const char *name) {
    if (!name) return NULL;
    /* The guest asks the bootstrap for exactly two names, at five call sites
     * (0x143541c, 0x1435438, 0x143544c, 0x143554c, 0x1435578). Anything else
     * is not something this runtime provides. */
    if (!strcmp(name, "nvnDeviceInitialize")) return (void *)(uintptr_t)nvn_device_initialize;
    if (!strcmp(name, "nvnDeviceGetProcAddress")) return (void *)(uintptr_t)nvn_device_get_proc_address;
    fprintf(stderr, "[nvn] nvnBootstrapLoader: \"%s\" is not one of the two names this runtime provides -> NULL\n",
            name);
    return NULL;
}

void *nvn_device_get_proc_address(void *device, const char *name) {
    size_t i;
    uint64_t slot;
    (void)device;
    s_device.resolved++;
    if (!name) {
        fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: NULL name -> NULL\n");
        return NULL;
    }
    if (strncmp(name, "nvn", 3) != 0) {
        fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" is outside the NVN namespace -> NULL\n", name);
        return NULL;
    }
    for (i = 0; i < s_implemented_count; i++) {
        if (!strcmp(name, s_implemented[i].name)) {
            s_device.implemented_resolved++;
            fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
            return s_implemented[i].fn;
        }
    }
    /* The other object families carry their own name tables, so that a family
     * can grow without touching this dispatch. */
    {
        size_t count = nvn_queue_family_count();
        for (i = 0; i < count; i++) {
            const nvn_queue_family_entry *entry = nvn_queue_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_window_family_count();
        for (i = 0; i < count; i++) {
            const nvn_window_family_entry *entry = nvn_window_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_memory_pool_family_count();
        for (i = 0; i < count; i++) {
            const nvn_memory_pool_family_entry *entry = nvn_memory_pool_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_blend_state_family_count();
        for (i = 0; i < count; i++) {
            const nvn_blend_state_family_entry *entry = nvn_blend_state_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_sampler_pool_family_count();
        for (i = 0; i < count; i++) {
            const nvn_sampler_pool_family_entry *entry = nvn_sampler_pool_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_texture_pool_family_count();
        for (i = 0; i < count; i++) {
            const nvn_texture_pool_family_entry *entry = nvn_texture_pool_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_render_state_family_count();
        for (i = 0; i < count; i++) {
            const nvn_render_state_family_entry *entry = nvn_render_state_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_program_family_count();
        for (i = 0; i < count; i++) {
            const nvn_program_family_entry *entry = nvn_program_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    /* Not implemented yet. The guest never checks the result, so it gets a
     * trap that names the API instead of a NULL. */
    slot = nvn_trap_allocate(name);
    s_device.trapped_resolved++;
    if (slot == UINT64_MAX) {
        fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> shared trap (no trap slot left)\n", name);
        return (void *)(uintptr_t)nvn_shared_trap;
    }
    fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> trap slot %" PRIu64 " (not implemented yet)\n",
            name, slot);
#if NVN_TRAP_TABLE_PRESENT
    return (void *)(uintptr_t)(nvn_trampoline_table + slot * NVN_TRAP_STRIDE);
#else
    return (void *)(uintptr_t)nvn_shared_trap;
#endif
}

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

void nvn_device_get_stats(nvn_device_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->resolved = s_device.resolved;
    out->implemented_resolved = s_device.implemented_resolved;
    out->trapped_resolved = s_device.trapped_resolved;
    out->calls = s_device.calls;
    out->unimplemented_calls = s_device.unimplemented_calls;
    out->initialized = s_device.initialized;
    out->real_device = s_device.real_device;
    out->flags = s_device.flags;
    out->trap_slots_used = s_trap_used;
    memcpy(out->description, s_device.description, sizeof(out->description));
}
