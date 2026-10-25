/* The contract of the NVN event family.
 *
 * An event is the signal between the GPU and the CPU. The guest fills an
 * event builder with the storage it wants (a memory pool and an offset inside
 * it), hands it to nvnEventInitialize, and from then on the event can be
 * signalled and its value read back. This case checks that the eleven names
 * of the family resolve through the guest's own lookup, that a signal is read
 * back with the value it was given, that an event is a snapshot of the builder
 * it was made from, and that the guest's own event, builder and pool memory is
 * not written.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a second
 * contract binary.
 *
 * The family is driven through the guest's own lookup, the way the guest
 * drives it: the bootstrap hands out nvnDeviceGetProcAddress, and every name
 * of the family is resolved through it. The implementation's functions are
 * not called directly, so that a name that the device does not hand out is a
 * failure of the contract and not a silent success.
 *
 * The expectations are written out as plain numbers, not as the constants of
 * the implementation, so that a change of the implementation that moves a
 * value is caught here.
 */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_device.h"
#include "../../src/graphics/nvn_device/nvn_event.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"event contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

static unsigned errors;

static void expect_value(const char *tag,uint64_t actual,uint64_t expected) {
    if(actual!=expected) {
        errors++;
        fprintf(stderr,"[check-fail] %s actual=0x%" PRIx64 " expected=0x%" PRIx64 "\n",tag,actual,expected);
    }
}

static void expect_condition(const char *tag,int condition) {
    if(!condition) {errors++;fprintf(stderr,"[check-fail] %s\n",tag);}
}

static int all_bytes(const void *p,size_t n,unsigned char value) {
    const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i]!=value)return 0;return 1;
}

typedef void *(*bootstrap_fn)(const char *);
typedef void *(*lookup_fn)(void *, const char *);
typedef uint64_t (*init_fn)(void *, void *);

/* The eleven entry points of the event family.
 *
 * The argument types are the ones of the vendored NVN header
 * (src/graphics/nvn/include/nvn/nvn.h:1747-1752) for the six names that
 * header has. Five of the eleven have no prototype there and no
 * implementation in the reference host (nvn_protos.h:517-519), and they are
 * answered the way the sibling getters of the buffer family are:
 *
 *   - nvnEventBuilderGetStorage answers the pool in the register the ABI
 *     returns a value in. The storage of an event builder is the pair (pool,
 *     offset) and the two halves are answered by the two getters below, so
 *     the pool is the half that fits in one register. A getter that wrote the
 *     pair through out-parameters would write into the guest's memory.
 *   - nvnEventBuilderGetMemoryPool answers the pool as a pointer, the way
 *     nvnBufferBuilderGetMemoryPool does: a pool of 256 opaque bytes cannot
 *     be handed to the guest as a value.
 *   - nvnEventBuilderGetMemoryOffset and nvnEventGetMemoryOffset answer the
 *     offset as a ptrdiff_t, the way nvnBufferGetMemoryOffset does.
 *   - nvnEventGetMemoryPool answers the pool as a pointer, the way
 *     nvnBufferGetMemoryPool does.
 */
typedef void (*builder_set_defaults_fn)(void *);
typedef void (*builder_set_storage_fn)(void *, void *, int64_t);
typedef void *(*builder_get_storage_fn)(const void *);
typedef void *(*builder_get_memory_pool_fn)(const void *);
typedef ptrdiff_t (*builder_get_memory_offset_fn)(const void *);
typedef int (*event_initialize_fn)(void *, const void *);
typedef void (*event_finalize_fn)(void *);
typedef uint32_t (*event_get_value_fn)(const void *);
typedef void (*event_signal_fn)(void *, int, uint32_t);
typedef void *(*event_get_memory_pool_fn)(const void *);
typedef ptrdiff_t (*event_get_memory_offset_fn)(const void *);

/* The memory pool family, so that the event lives in a real pool and an
 * initialization with storage can succeed. */
typedef void (*pool_builder_set_defaults_fn)(void *);
typedef void (*pool_builder_set_device_fn)(void *, void *);
typedef void (*pool_builder_set_storage_fn)(void *, void *, uint64_t);
typedef int (*pool_initialize_fn)(const void *, const void *);

/* The values this case puts into the builder. They are written out here and
 * not taken from the implementation, so that a change of the implementation
 * that moves a value is caught. */
#define TEST_MEMORY_OFFSET 256
#define TEST_OTHER_OFFSET 1024
#define TEST_POOL_SIZE 65536u

/* The values this case signals with, and the values it expects to read back.
 * The signal modes are the ones of the vendored header (nvn.h:661-665): the
 * mode WRITE writes the value, the mode ADD adds to it, and every other mode
 * writes it as well, which is what the reference host does (hle_nvn.c:653). */
#define TEST_SIGNAL_MODE_WRITE 0
#define TEST_SIGNAL_MODE_ADD 1
#define TEST_SIGNAL_MODE_OTHER 3
#define TEST_SIGNAL_VALUE 0x5a5a5a5au
#define TEST_SIGNAL_ADDEND 7u
#define TEST_SIGNAL_VALUE_OTHER 0x11111111u

/* The value the signal of this case has to add up to. It is written out here
 * and not derived from the constants above, so that a change of one of them is
 * caught here as well. */
#define TEST_VALUE_AFTER_WRITE 0x5a5a5a5au
#define TEST_VALUE_AFTER_ADD 0x5a5a5a61u
#define TEST_VALUE_AFTER_OTHER 0x11111111u

/* ------------------------------------------------------------------ */
/* A device for this contract                                           */
/* ------------------------------------------------------------------ */

/* A memory pool needs a device that really exists: a pool without a device
 * is memory that no graphics device can use, and the memory pool family
 * refuses to make one (nvn_memory_pool.c). The host build of this runtime has
 * no Vulkan implementation, so it has no device, and without a device there is
 * no pool, and without a pool an event has no storage.
 *
 * The address of an event is the one thing this contract cannot check
 * without a pool, so this contract installs a backend of its own that
 * reports a device. The backend is the platform half of the device layer,
 * and nvn_device_set_backend is the seam the runtime offers for it. What is
 * measured here is the storage of an event and the value of a signal: all of
 * that is the runtime's own code, with the guest's own memory behind it. What
 * this does NOT prove is that a graphics device exists; that is what the
 * device is for, and the device is the coordinator's.
 *
 * On a build with a real graphics device (Android, with the Vulkan backend)
 * the real backend is left in place and the real device is used, and the
 * backend of this contract is not even compiled. */
#if !defined(__ANDROID__)
static int contract_backend_create(nvn_device_state *device, const void *builder) {
    (void)device;
    (void)builder;
    return 1;
}

static void contract_backend_destroy(nvn_device_state *device) { (void)device; }

static void contract_backend_describe(const nvn_device_state *device, char *out, size_t out_size) {
    (void)device;
    snprintf(out, out_size, "the backend of the NVN event contract, which reports a device and no graphics hardware");
}

static int contract_backend_get_queue(nvn_device_state *device, void **out_queue) {
    (void)device;
    *out_queue = NULL;
    return 0;
}

static int contract_backend_create_window_surface(nvn_device_state *device, void *native_window,
                                                  uint32_t *out_width, uint32_t *out_height,
                                                  void **out_surface, void **out_swapchain) {
    (void)device;
    (void)native_window;
    *out_width = 0;
    *out_height = 0;
    *out_surface = NULL;
    *out_swapchain = NULL;
    return 0;
}

static void contract_backend_destroy_window_surface(nvn_device_state *device, void *surface, void *swapchain) {
    (void)device;
    (void)surface;
    (void)swapchain;
}

/* The three steps of the present path. This contract has no swapchain, so
 * every step refuses and says why. A step that reported success would be a
 * frame that was never shown. */
static int contract_backend_acquire_image(nvn_device_state *device, uint32_t *out_index, void **out_image,
                                          char *reason, size_t reason_size) {
    (void)device;
    if (out_index) *out_index = 0;
    if (out_image) *out_image = NULL;
    if (reason && reason_size) snprintf(reason, reason_size, "the contract backend has no swapchain");
    return 0;
}

static int contract_backend_begin_render_pass(nvn_device_state *device, uint32_t index, void *image,
                                              const float rgba[4], char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    (void)rgba;
    if (reason && reason_size) snprintf(reason, reason_size, "the contract backend has no render pass");
    return 0;
}

static int contract_backend_submit_and_present(nvn_device_state *device, uint32_t index, void *image,
                                               char *reason, size_t reason_size) {
    (void)device;
    (void)index;
    (void)image;
    if (reason && reason_size) snprintf(reason, reason_size, "the contract backend never presents anything");
    return 0;
}

static const nvn_backend s_contract_backend = {
    "event-contract",
    contract_backend_create,
    contract_backend_destroy,
    contract_backend_describe,
    contract_backend_get_queue,
    contract_backend_create_window_surface,
    contract_backend_destroy_window_surface,
    /* The present path of the device layer. This contract is about events
     * and has no swapchain, so every step of the path refuses and says so.
     * The three methods have to be there: a backend without them would have a
     * vtable that does not match the one the runtime uses. */
    contract_backend_acquire_image,
    contract_backend_begin_render_pass,
    contract_backend_submit_and_present,
};
#endif /* !__ANDROID__ */

static int nvn_event_contract(void) {
    bootstrap_fn bootstrap;
    lookup_fn lookup;
    init_fn init;
    builder_set_defaults_fn builder_set_defaults;
    builder_set_storage_fn builder_set_storage;
    builder_get_storage_fn builder_get_storage;
    builder_get_memory_pool_fn builder_get_memory_pool;
    builder_get_memory_offset_fn builder_get_memory_offset;
    event_initialize_fn event_initialize;
    event_finalize_fn event_finalize;
    event_get_value_fn event_get_value;
    event_signal_fn event_signal;
    event_get_memory_pool_fn event_get_memory_pool;
    event_get_memory_offset_fn event_get_memory_offset;
    pool_builder_set_defaults_fn pool_builder_set_defaults;
    pool_builder_set_device_fn pool_builder_set_device;
    pool_builder_set_storage_fn pool_builder_set_storage;
    pool_initialize_fn pool_initialize;
    nvn_event_stats stats;
    nvn_device_stats device_stats;
    unsigned char builder_one[64];
    unsigned char builder_two[64];
    unsigned char builder_unprepared[64];
    unsigned char event_one[64];
    unsigned char event_two[64];
    unsigned char event_unknown[64];
    unsigned char pool_builder[64];
    unsigned char pool_object[64];
    unsigned char pool_storage[TEST_POOL_SIZE];
    void *device = (void *)(uintptr_t)0x9000;
    uint64_t init_result = 0;
    uint32_t pool_result = 0;
    int pool_exists = 0;
    size_t count;
    size_t i;
    size_t j;

    memset(builder_one, 0xaa, sizeof(builder_one));
    memset(builder_two, 0xaa, sizeof(builder_two));
    memset(builder_unprepared, 0xaa, sizeof(builder_unprepared));
    memset(event_one, 0xaa, sizeof(event_one));
    memset(event_two, 0xaa, sizeof(event_two));
    memset(event_unknown, 0xaa, sizeof(event_unknown));
    memset(pool_builder, 0xaa, sizeof(pool_builder));
    memset(pool_object, 0xaa, sizeof(pool_object));
    memset(pool_storage, 0xcc, sizeof(pool_storage));

    /* 1. Every name of the family resolves, resolves to the same entry point
     *    every time, and the family table holds each of them once. */
    bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0, "nvnBootstrapLoader");
    CHECK(bootstrap != NULL);
    lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup != NULL);
    count = nvn_event_family_count();
    expect_value("nvn-event-family-has-eleven-names", (uint64_t)count, 11);
    for (i = 0; i < count; i++) {
        const nvn_event_family_entry *entry = nvn_event_family_entry_at(i);
        void *resolved_name;
        CHECK(entry != NULL);
        CHECK(entry->name != NULL);
        resolved_name = lookup(NULL, entry->name);
        expect_condition("nvn-event-name-resolves", resolved_name != NULL);
        expect_condition("nvn-event-name-is-stable", resolved_name == lookup(NULL, entry->name));
        expect_condition("nvn-event-name-matches-the-table", resolved_name == entry->fn);
        for (j = i + 1; j < count; j++) {
            const nvn_event_family_entry *other = nvn_event_family_entry_at(j);
            expect_condition("nvn-event-family-holds-each-name-once",
                             other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
        }
    }

    /* The family itself, through the guest's lookup. */
    builder_set_defaults = (builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnEventBuilderSetDefaults");
    builder_set_storage = (builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnEventBuilderSetStorage");
    builder_get_storage = (builder_get_storage_fn)(uintptr_t)lookup(NULL, "nvnEventBuilderGetStorage");
    builder_get_memory_pool = (builder_get_memory_pool_fn)(uintptr_t)lookup(NULL, "nvnEventBuilderGetMemoryPool");
    builder_get_memory_offset = (builder_get_memory_offset_fn)(uintptr_t)lookup(NULL, "nvnEventBuilderGetMemoryOffset");
    event_initialize = (event_initialize_fn)(uintptr_t)lookup(NULL, "nvnEventInitialize");
    event_finalize = (event_finalize_fn)(uintptr_t)lookup(NULL, "nvnEventFinalize");
    event_get_value = (event_get_value_fn)(uintptr_t)lookup(NULL, "nvnEventGetValue");
    event_signal = (event_signal_fn)(uintptr_t)lookup(NULL, "nvnEventSignal");
    event_get_memory_pool = (event_get_memory_pool_fn)(uintptr_t)lookup(NULL, "nvnEventGetMemoryPool");
    event_get_memory_offset = (event_get_memory_offset_fn)(uintptr_t)lookup(NULL, "nvnEventGetMemoryOffset");
    CHECK(builder_set_defaults && builder_set_storage && builder_get_storage);
    CHECK(builder_get_memory_pool && builder_get_memory_offset);
    CHECK(event_initialize && event_finalize && event_get_value && event_signal);
    CHECK(event_get_memory_pool && event_get_memory_offset);

    /* The memory pool family, so that the event lives in a real pool. */
    pool_builder_set_defaults = (pool_builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDefaults");
    pool_builder_set_device = (pool_builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDevice");
    pool_builder_set_storage = (pool_builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetStorage");
    pool_initialize = (pool_initialize_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolInitialize");
    CHECK(pool_builder_set_defaults && pool_builder_set_device && pool_builder_set_storage && pool_initialize);

    /* 2. The device and the memory pool. A memory pool needs a device that
     *    really exists, so the backend of this contract reports one. What is
     *    measured is the pool and the event, not a graphics device. */
#if !defined(__ANDROID__)
    nvn_device_set_backend(&s_contract_backend);
#endif
    init = (init_fn)(uintptr_t)bootstrap("nvnDeviceInitialize");
    CHECK(init != NULL);
    init_result = init(device, NULL);
    nvn_device_get_stats(&device_stats);
    expect_condition("nvn-event-device-initialised-matches-reality",
                     (init_result != 0) == (device_stats.real_device != 0));
    expect_condition("nvn-event-a-device-exists-for-the-pool", device_stats.real_device != 0);
    pool_builder_set_defaults(pool_builder);
    pool_builder_set_device(pool_builder, device);
    pool_builder_set_storage(pool_builder, pool_storage, (uint64_t)sizeof(pool_storage));
    pool_result = (uint32_t)pool_initialize(pool_object, pool_builder);
    expect_value("nvn-event-pool-init-matches-the-device", (uint64_t)pool_result,
                 (uint64_t)(device_stats.real_device ? 1 : 0));
    pool_exists = (pool_result != 0);
    /* The event needs a pool that has memory, so the pool has to be there.
     * The backend of this contract reports a device, so it is. */
    expect_condition("nvn-event-a-pool-with-memory-exists-for-the-event", pool_exists != 0);

    /* 3. The builder. The defaults clear the storage, and the storage the
     *    guest sets is the storage the getters answer with. */
    builder_set_defaults(builder_one);
    expect_condition("nvn-event-builder-defaults-have-no-pool", builder_get_memory_pool(builder_one) == NULL);
    expect_value("nvn-event-builder-defaults-have-no-offset", (uint64_t)(int64_t)builder_get_memory_offset(builder_one),
                 0);
    expect_condition("nvn-event-builder-defaults-have-no-storage", builder_get_storage(builder_one) == NULL);
    builder_set_storage(builder_one, pool_object, (int64_t)TEST_MEMORY_OFFSET);
    expect_condition("nvn-event-builder-storage-round-trips", builder_get_memory_pool(builder_one) == pool_object);
    expect_value("nvn-event-builder-memory-offset-round-trips", (uint64_t)(int64_t)builder_get_memory_offset(builder_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);
    expect_condition("nvn-event-builder-get-storage-answers-the-pool", builder_get_storage(builder_one) == pool_object);

    /* 4. An event is made out of the builder, and the event holds the
     *    description of the builder. */
    expect_condition("nvn-event-initialize-succeeds", event_initialize(event_one, builder_one) == 1);
    expect_condition("nvn-event-memory-pool-round-trips", event_get_memory_pool(event_one) == pool_object);
    expect_value("nvn-event-memory-offset-round-trips", (uint64_t)(int64_t)event_get_memory_offset(event_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);

    /* 5. The value of an event before it is signalled. The value lives on the
     *    host, so there is nothing there yet. */
    expect_value("nvn-event-value-before-a-signal-is-zero", (uint64_t)event_get_value(event_one), 0);

    /* 6. A signal is read back with the value it was given. This is the
     *    point of the family: the signal between the GPU and the CPU has to
     *    be readable. */
    event_signal(event_one, TEST_SIGNAL_MODE_WRITE, TEST_SIGNAL_VALUE);
    expect_value("nvn-event-value-after-a-write-signal", (uint64_t)event_get_value(event_one),
                 (uint64_t)TEST_VALUE_AFTER_WRITE);
    event_signal(event_one, TEST_SIGNAL_MODE_ADD, TEST_SIGNAL_ADDEND);
    expect_value("nvn-event-value-after-an-add-signal", (uint64_t)event_get_value(event_one),
                 (uint64_t)TEST_VALUE_AFTER_ADD);
    event_signal(event_one, TEST_SIGNAL_MODE_OTHER, TEST_SIGNAL_VALUE_OTHER);
    expect_value("nvn-event-value-after-a-signal-in-another-mode", (uint64_t)event_get_value(event_one),
                 (uint64_t)TEST_VALUE_AFTER_OTHER);

    /* 7. The guest's own objects were not written to. The layout of an event
     *    and of a builder is a property of the Nintendo SDK, so the runtime
     *    keeps its state on the host. The memory of the pool is the guest's
     *    memory as well, and the value of the event is not written there
     *    either: the reference host does write it there (hle_nvn.c:649) and
     *    this runtime does not, which is the difference the two sentinels
     *    below make visible. */
    expect_condition("nvn-event-builder-not-written", all_bytes(builder_one, sizeof(builder_one), 0xaa));
    expect_condition("nvn-event-object-not-written", all_bytes(event_one, sizeof(event_one), 0xaa));
    expect_condition("nvn-event-pool-memory-not-written",
                     all_bytes(pool_storage + TEST_MEMORY_OFFSET, 4, 0xcc));

    /* 8. An event is a snapshot of the builder it was made from. The builder
     *    changes here, and the event must not. */
    builder_set_storage(builder_one, pool_object, (int64_t)TEST_OTHER_OFFSET);
    expect_value("nvn-event-snapshot-keeps-the-memory-offset", (uint64_t)(int64_t)event_get_memory_offset(event_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);
    expect_condition("nvn-event-snapshot-keeps-the-memory-pool", event_get_memory_pool(event_one) == pool_object);
    expect_value("nvn-event-builder-memory-offset-changed", (uint64_t)(int64_t)builder_get_memory_offset(builder_one),
                 (uint64_t)(int64_t)TEST_OTHER_OFFSET);
    expect_value("nvn-event-snapshot-keeps-the-value", (uint64_t)event_get_value(event_one),
                 (uint64_t)TEST_VALUE_AFTER_OTHER);

    /* 9. A second event is made out of a second builder, and the two events
     *    do not share their value. */
    builder_set_defaults(builder_two);
    builder_set_storage(builder_two, pool_object, (int64_t)TEST_OTHER_OFFSET);
    expect_condition("nvn-event-initialize-succeeds-again", event_initialize(event_two, builder_two) == 1);
    expect_value("nvn-event-second-memory-offset-round-trips",
                 (uint64_t)(int64_t)event_get_memory_offset(event_two), (uint64_t)(int64_t)TEST_OTHER_OFFSET);
    expect_value("nvn-event-second-value-starts-at-zero", (uint64_t)event_get_value(event_two), 0);
    event_signal(event_two, TEST_SIGNAL_MODE_WRITE, TEST_SIGNAL_VALUE);
    expect_value("nvn-event-second-value-after-a-write-signal", (uint64_t)event_get_value(event_two),
                 (uint64_t)TEST_VALUE_AFTER_WRITE);
    expect_value("nvn-event-first-value-is-not-the-second-value", (uint64_t)event_get_value(event_one),
                 (uint64_t)TEST_VALUE_AFTER_OTHER);

    /* 10. The refusals. A builder that was never prepared has no storage, and
     *     an event that was never initialized does not exist. */
    expect_condition("nvn-event-initialize-refuses-an-unprepared-builder",
                     event_initialize(event_unknown, builder_unprepared) == 0);
    expect_value("nvn-event-value-of-an-unknown-event-is-zero", (uint64_t)event_get_value(event_unknown), 0);
    expect_condition("nvn-event-memory-pool-of-an-unknown-event-is-null", event_get_memory_pool(event_unknown) == NULL);
    expect_value("nvn-event-memory-offset-of-an-unknown-event-is-zero",
                 (uint64_t)(int64_t)event_get_memory_offset(event_unknown), 0);
    /* A signal on an event that does not exist changes nothing. */
    event_signal(event_unknown, TEST_SIGNAL_MODE_WRITE, TEST_SIGNAL_VALUE);
    expect_value("nvn-event-signal-on-an-unknown-event-changes-nothing", (uint64_t)event_get_value(event_unknown), 0);

    /* 11. Finalizing an event takes it out of the runtime's table. */
    event_finalize(event_two);
    expect_value("nvn-event-value-of-a-finalized-event-is-zero", (uint64_t)event_get_value(event_two), 0);
    expect_condition("nvn-event-memory-pool-of-a-finalized-event-is-null", event_get_memory_pool(event_two) == NULL);
    expect_value("nvn-event-memory-offset-of-a-finalized-event-is-zero",
                 (uint64_t)(int64_t)event_get_memory_offset(event_two), 0);

    /* 12. The guest's objects are still untouched after all of that. */
    expect_condition("nvn-event-builder-not-written-afterwards",
                     all_bytes(builder_one, sizeof(builder_one), 0xaa) &&
                         all_bytes(builder_two, sizeof(builder_two), 0xaa) &&
                         all_bytes(builder_unprepared, sizeof(builder_unprepared), 0xaa));
    expect_condition("nvn-event-object-not-written-afterwards",
                     all_bytes(event_one, sizeof(event_one), 0xaa) &&
                         all_bytes(event_two, sizeof(event_two), 0xaa) &&
                         all_bytes(event_unknown, sizeof(event_unknown), 0xaa));
    expect_condition("nvn-event-pool-memory-not-written-afterwards",
                     all_bytes(pool_storage + TEST_MEMORY_OFFSET, 4, 0xcc));

    /* 13. The counters. */
    nvn_event_get_stats(&stats);
    expect_condition("nvn-event-stats-count-calls", stats.calls > 0);
    expect_condition("nvn-event-stats-count-builder-calls", stats.builder_calls > 0);
    expect_condition("nvn-event-stats-count-initialized", stats.initialized >= 2);
    expect_condition("nvn-event-stats-count-finalized", stats.finalized >= 1);
    expect_condition("nvn-event-stats-count-signals", stats.signals >= 4);
    expect_condition("nvn-event-stats-count-gets", stats.gets > 0);
    expect_condition("nvn-event-stats-count-refusals", stats.refused > 0);
    expect_condition("nvn-event-stats-have-live-events", stats.live_events >= 1);
    expect_condition("nvn-event-stats-have-live-builders", stats.live_builders >= 1);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_event_errors\":%u,\"names\":%zu,\"initialized\":%llu,\"finalized\":%llu,"
           "\"signals\":%llu,\"gets\":%llu,\"refused\":%llu,"
           "\"live_events\":%d,\"live_builders\":%d}\n",
           errors, count, (unsigned long long)stats.initialized, (unsigned long long)stats.finalized,
           (unsigned long long)stats.signals, (unsigned long long)stats.gets, (unsigned long long)stats.refused,
           stats.live_events, stats.live_builders);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-event")) {
        fprintf(stderr,"usage: %s --nvn-event\n",argv[0]);
        return 2;
    }
    return nvn_event_contract();
}
