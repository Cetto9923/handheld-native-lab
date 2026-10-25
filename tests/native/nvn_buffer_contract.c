/* The contract of the NVN buffer object family.
 *
 * A buffer is what the guest makes out of a buffer builder: it fills a
 * builder with the storage it wants (a memory pool and an offset inside it),
 * hands it to nvnBufferInitialize, and from then on the buffer is the object
 * its draws refer to. This case checks that the buffer really holds what the
 * builder was given, that the guest's own builder and buffer memory is not
 * written, that a buffer is a snapshot of the builder it was made from, and
 * that the address of a buffer is an address of the guest that the memory
 * pool family can resolve back into host memory.
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
#include "../../src/graphics/nvn_device/nvn_buffer.h"
#include "../../src/graphics/nvn_device/nvn_device.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"buffer contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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

/* The eighteen entry points of the buffer family.
 *
 * The argument types are the ones of the vendored NVN header
 * (src/graphics/nvn/include/nvn/nvn.h:1234-1251). Two of them are different
 * from the header:
 *
 *   - nvnBufferBuilderGetDevice has no signature in that header at all, and
 *     the reference tree lists it among the names it can only give a stub
 *     (nvn_protos.h:506). Its arguments are therefore taken as plain words,
 *     the way nvnTextureBuilderGetDevice does it, and the device comes back
 *     in the register the ABI returns a value in.
 *   - nvnBufferBuilderGetMemoryPool is written in that header as returning
 *     the pool by value (nvn.h:1238), which is 256 opaque bytes and cannot be
 *     handed to the guest through a pointer. The sibling getter
 *     nvnBufferGetMemoryPool returns a pointer (nvn.h:1248), and so does
 *     this one.
 */
typedef void (*builder_set_defaults_fn)(void *);
typedef void (*builder_set_device_fn)(void *, void *);
typedef void (*builder_set_storage_fn)(void *, void *, ptrdiff_t, size_t);
typedef uint64_t (*builder_get_device_fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
typedef size_t (*builder_get_size_fn)(const void *);
typedef ptrdiff_t (*builder_get_memory_offset_fn)(const void *);
typedef void *(*builder_get_memory_pool_fn)(const void *);
typedef int (*buffer_initialize_fn)(void *, const void *);
typedef void (*buffer_finalize_fn)(void *);
typedef void (*buffer_set_debug_label_fn)(void *, const char *);
typedef uint64_t (*buffer_get_debug_id_fn)(const void *);
typedef uint64_t (*buffer_get_address_fn)(const void *);
typedef void *(*buffer_map_fn)(const void *);
typedef size_t (*buffer_get_size_fn)(const void *);
typedef void *(*buffer_get_memory_pool_fn)(const void *);
typedef ptrdiff_t (*buffer_get_memory_offset_fn)(const void *);
typedef void (*buffer_flush_mapped_range_fn)(const void *, ptrdiff_t, size_t);
typedef void (*buffer_invalidate_mapped_range_fn)(const void *, ptrdiff_t, size_t);

/* The memory pool family, so that the buffer lives in a real pool and the
 * address of the buffer is an address of the guest. */
typedef void (*pool_builder_set_defaults_fn)(void *);
typedef void (*pool_builder_set_device_fn)(void *, void *);
typedef void (*pool_builder_set_storage_fn)(void *, void *, uint64_t);
typedef int (*pool_initialize_fn)(const void *, const void *);

/* The values this case puts into the builder. They are written out here and
 * not taken from the implementation, so that a change of the implementation
 * that moves a value is caught. */
#define TEST_BUFFER_SIZE 4096u
#define TEST_MEMORY_OFFSET 256
#define TEST_POOL_SIZE 65536u
#define TEST_OTHER_SIZE 8192u
#define TEST_OTHER_OFFSET 1024

/* ------------------------------------------------------------------ */
/* A device for this contract                                           */
/* ------------------------------------------------------------------ */

/* A memory pool needs a device that really exists: a pool without a device
 * is memory that no graphics device can use, and the memory pool family
 * refuses to make one (nvn_memory_pool.c). The host build of this runtime has
 * no Vulkan implementation, so it has no device, and without a device there
 * is no pool, and without a pool a buffer has no address.
 *
 * The address of a buffer is the one thing this contract cannot check
 * without a pool, so this contract installs a backend of its own that
 * reports a device. The backend is the platform half of the device layer,
 * and nvn_device_set_backend is the seam the runtime offers for it. What is
 * measured here is the address arithmetic of the buffer family and the
 * resolution of that address by the memory pool family: all of that is the
 * runtime's own code, with the guest's own memory behind it. What this does
 * NOT prove is that a graphics device exists; that is what the device is for,
 * and the device is the coordinator's.
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
    snprintf(out, out_size, "the backend of the NVN buffer contract, which reports a device and no graphics hardware");
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
    "buffer-contract",
    contract_backend_create,
    contract_backend_destroy,
    contract_backend_describe,
    contract_backend_get_queue,
    contract_backend_create_window_surface,
    contract_backend_destroy_window_surface,
    /* The present path of the device layer. This contract is about buffers
     * and has no swapchain, so every step of the path refuses and says so.
     * The three methods have to be there: a backend without them would have a
     * vtable that does not match the one the runtime uses. */
    contract_backend_acquire_image,
    contract_backend_begin_render_pass,
    contract_backend_submit_and_present,
};
#endif /* !__ANDROID__ */

/* Fill a builder with the values of this case. */
static void fill_builder(builder_set_defaults_fn set_defaults, builder_set_device_fn set_device,
                         builder_set_storage_fn set_storage, void *builder, void *device, void *memory_pool,
                         ptrdiff_t offset, size_t size) {
    set_defaults(builder);
    set_device(builder, device);
    set_storage(builder, memory_pool, offset, size);
}

static int nvn_buffer_contract(void) {
    bootstrap_fn bootstrap;
    lookup_fn lookup;
    init_fn init;
    builder_set_defaults_fn builder_set_defaults;
    builder_set_device_fn builder_set_device;
    builder_set_storage_fn builder_set_storage;
    builder_get_device_fn builder_get_device;
    builder_get_size_fn builder_get_size;
    builder_get_memory_offset_fn builder_get_memory_offset;
    builder_get_memory_pool_fn builder_get_memory_pool;
    buffer_initialize_fn buffer_initialize;
    buffer_finalize_fn buffer_finalize;
    buffer_set_debug_label_fn buffer_set_debug_label;
    buffer_get_debug_id_fn buffer_get_debug_id;
    buffer_get_address_fn buffer_get_address;
    buffer_map_fn buffer_map;
    buffer_get_size_fn buffer_get_size;
    buffer_get_memory_pool_fn buffer_get_memory_pool;
    buffer_get_memory_offset_fn buffer_get_memory_offset;
    buffer_flush_mapped_range_fn buffer_flush_mapped_range;
    buffer_invalidate_mapped_range_fn buffer_invalidate_mapped_range;
    pool_builder_set_defaults_fn pool_builder_set_defaults;
    pool_builder_set_device_fn pool_builder_set_device;
    pool_builder_set_storage_fn pool_builder_set_storage;
    pool_initialize_fn pool_initialize;
    nvn_buffer_stats stats;
    nvn_device_stats device_stats;
    unsigned char builder_one[64];
    unsigned char builder_two[64];
    unsigned char builder_unprepared[64];
    unsigned char buffer_one[64];
    unsigned char buffer_two[64];
    unsigned char buffer_unknown[64];
    unsigned char pool_builder[64];
    unsigned char pool_object[64];
    unsigned char pool_storage[TEST_POOL_SIZE];
    void *device = (void *)(uintptr_t)0x9000;
    uint64_t init_result = 0;
    uint64_t address_one = 0;
    uint64_t address_two = 0;
    uint64_t debug_id_one = 0;
    uint64_t debug_id_two = 0;
    uint32_t pool_result = 0;
    size_t available = 0;
    void *resolved = NULL;
    int pool_exists = 0;
    size_t count;
    size_t i;
    size_t j;

    memset(builder_one, 0xaa, sizeof(builder_one));
    memset(builder_two, 0xaa, sizeof(builder_two));
    memset(builder_unprepared, 0xaa, sizeof(builder_unprepared));
    memset(buffer_one, 0xaa, sizeof(buffer_one));
    memset(buffer_two, 0xaa, sizeof(buffer_two));
    memset(buffer_unknown, 0xaa, sizeof(buffer_unknown));
    memset(pool_builder, 0xaa, sizeof(pool_builder));
    memset(pool_object, 0xaa, sizeof(pool_object));
    memset(pool_storage, 0xcc, sizeof(pool_storage));

    /* 1. Every name of the family resolves, resolves to the same entry point
     *    every time, and the family table holds each of them once. */
    bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0, "nvnBootstrapLoader");
    CHECK(bootstrap != NULL);
    lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup != NULL);
    count = nvn_buffer_family_count();
    expect_value("nvn-buffer-family-has-eighteen-names", (uint64_t)count, 18);
    for (i = 0; i < count; i++) {
        const nvn_buffer_family_entry *entry = nvn_buffer_family_entry_at(i);
        void *resolved_name;
        CHECK(entry != NULL);
        CHECK(entry->name != NULL);
        resolved_name = lookup(NULL, entry->name);
        expect_condition("nvn-buffer-name-resolves", resolved_name != NULL);
        expect_condition("nvn-buffer-name-is-stable", resolved_name == lookup(NULL, entry->name));
        expect_condition("nvn-buffer-name-matches-the-table", resolved_name == entry->fn);
        for (j = i + 1; j < count; j++) {
            const nvn_buffer_family_entry *other = nvn_buffer_family_entry_at(j);
            expect_condition("nvn-buffer-family-holds-each-name-once",
                             other == NULL || other->name == NULL || strcmp(entry->name, other->name) != 0);
        }
    }

    /* The family itself, through the guest's lookup. */
    builder_set_defaults = (builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderSetDefaults");
    builder_set_device = (builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderSetDevice");
    builder_set_storage = (builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderSetStorage");
    builder_get_device = (builder_get_device_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderGetDevice");
    builder_get_size = (builder_get_size_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderGetSize");
    builder_get_memory_offset = (builder_get_memory_offset_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderGetMemoryOffset");
    builder_get_memory_pool = (builder_get_memory_pool_fn)(uintptr_t)lookup(NULL, "nvnBufferBuilderGetMemoryPool");
    buffer_initialize = (buffer_initialize_fn)(uintptr_t)lookup(NULL, "nvnBufferInitialize");
    buffer_finalize = (buffer_finalize_fn)(uintptr_t)lookup(NULL, "nvnBufferFinalize");
    buffer_set_debug_label = (buffer_set_debug_label_fn)(uintptr_t)lookup(NULL, "nvnBufferSetDebugLabel");
    buffer_get_debug_id = (buffer_get_debug_id_fn)(uintptr_t)lookup(NULL, "nvnBufferGetDebugID");
    buffer_get_address = (buffer_get_address_fn)(uintptr_t)lookup(NULL, "nvnBufferGetAddress");
    buffer_map = (buffer_map_fn)(uintptr_t)lookup(NULL, "nvnBufferMap");
    buffer_get_size = (buffer_get_size_fn)(uintptr_t)lookup(NULL, "nvnBufferGetSize");
    buffer_get_memory_pool = (buffer_get_memory_pool_fn)(uintptr_t)lookup(NULL, "nvnBufferGetMemoryPool");
    buffer_get_memory_offset = (buffer_get_memory_offset_fn)(uintptr_t)lookup(NULL, "nvnBufferGetMemoryOffset");
    buffer_flush_mapped_range = (buffer_flush_mapped_range_fn)(uintptr_t)lookup(NULL, "nvnBufferFlushMappedRange");
    buffer_invalidate_mapped_range =
        (buffer_invalidate_mapped_range_fn)(uintptr_t)lookup(NULL, "nvnBufferInvalidateMappedRange");
    CHECK(builder_set_defaults && builder_set_device && builder_set_storage);
    CHECK(builder_get_device && builder_get_size && builder_get_memory_offset && builder_get_memory_pool);
    CHECK(buffer_initialize && buffer_finalize && buffer_set_debug_label && buffer_get_debug_id);
    CHECK(buffer_get_address && buffer_map && buffer_get_size && buffer_get_memory_pool);
    CHECK(buffer_get_memory_offset && buffer_flush_mapped_range && buffer_invalidate_mapped_range);

    /* The memory pool family, so that the buffer lives in a real pool and the
     * address of the buffer is an address of the guest. */
    pool_builder_set_defaults = (pool_builder_set_defaults_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDefaults");
    pool_builder_set_device = (pool_builder_set_device_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetDevice");
    pool_builder_set_storage = (pool_builder_set_storage_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolBuilderSetStorage");
    pool_initialize = (pool_initialize_fn)(uintptr_t)lookup(NULL, "nvnMemoryPoolInitialize");
    CHECK(pool_builder_set_defaults && pool_builder_set_device && pool_builder_set_storage && pool_initialize);

    /* 2. The device and the memory pool. A memory pool needs a device that
     *    really exists, so the backend of this contract reports one. What is
     *    measured is the pool and the buffer, not a graphics device. */
#if !defined(__ANDROID__)
    nvn_device_set_backend(&s_contract_backend);
#endif
    init = (init_fn)(uintptr_t)bootstrap("nvnDeviceInitialize");
    CHECK(init != NULL);
    init_result = init(device, NULL);
    nvn_device_get_stats(&device_stats);
    expect_condition("nvn-buffer-device-initialised-matches-reality",
                     (init_result != 0) == (device_stats.real_device != 0));
    expect_condition("nvn-buffer-a-device-exists-for-the-pool", device_stats.real_device != 0);
    pool_builder_set_defaults(pool_builder);
    pool_builder_set_device(pool_builder, device);
    pool_builder_set_storage(pool_builder, pool_storage, (uint64_t)sizeof(pool_storage));
    pool_result = (uint32_t)pool_initialize(pool_object, pool_builder);
    expect_value("nvn-buffer-pool-init-matches-the-device", (uint64_t)pool_result,
                 (uint64_t)(device_stats.real_device ? 1 : 0));
    pool_exists = (pool_result != 0);

    /* 3. A builder is filled, a buffer is made out of it, and every value of
     *    the description is read back. */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_storage, builder_one, device, pool_object,
                 (ptrdiff_t)TEST_MEMORY_OFFSET, (size_t)TEST_BUFFER_SIZE);
    expect_condition("nvn-buffer-initialize-succeeds", buffer_initialize(buffer_one, builder_one) == 1);
    buffer_set_debug_label(buffer_one, "contract-buffer-one");

    expect_value("nvn-buffer-size-round-trips", (uint64_t)buffer_get_size(buffer_one), (uint64_t)TEST_BUFFER_SIZE);
    expect_value("nvn-buffer-memory-offset-round-trips", (uint64_t)(int64_t)buffer_get_memory_offset(buffer_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);
    expect_condition("nvn-buffer-memory-pool-round-trips", buffer_get_memory_pool(buffer_one) == pool_object);
    expect_value("nvn-buffer-builder-size-round-trips", (uint64_t)builder_get_size(builder_one),
                 (uint64_t)TEST_BUFFER_SIZE);
    expect_value("nvn-buffer-builder-memory-offset-round-trips", (uint64_t)(int64_t)builder_get_memory_offset(builder_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);
    expect_condition("nvn-buffer-builder-memory-pool-round-trips", builder_get_memory_pool(builder_one) == pool_object);
    expect_value("nvn-buffer-builder-device-round-trips",
                 builder_get_device((uint64_t)(uintptr_t)builder_one, 0, 0, 0, 0, 0, 0, 0),
                 (uint64_t)(uintptr_t)device);

    /* 4. The address of the buffer is the guest's own memory, and the memory
     *    pool family resolves it back into host memory. */
    address_one = buffer_get_address(buffer_one);
    if (pool_exists) {
        expect_condition("nvn-buffer-address-is-not-zero", address_one != 0);
        /* The address has to be the address of the pool's memory plus the
         * offset of the buffer inside it. */
        expect_value("nvn-buffer-address-is-the-pool-memory-plus-the-offset", address_one,
                     (uint64_t)(uintptr_t)pool_storage + (uint64_t)TEST_MEMORY_OFFSET);
        /* And the memory pool family has to resolve it, which is what the
         * command buffer's replay does with the address the guest records. */
        available = 0;
        resolved = nvn_memory_pool_resolve_address(address_one, &available);
        expect_condition("nvn-buffer-address-resolves-in-the-memory-pool", resolved != NULL);
        expect_condition("nvn-buffer-address-resolves-to-the-same-bytes",
                         resolved == (void *)(uintptr_t)address_one);
        expect_condition("nvn-buffer-address-has-room-for-the-buffer", available >= (size_t)TEST_BUFFER_SIZE);
        /* The map is the same address as the one the getter hands out. */
        expect_condition("nvn-buffer-map-is-the-same-address", buffer_map(buffer_one) == (void *)(uintptr_t)address_one);
    } else {
        /* Without a pool there is no address. That is the truth of a build
         * without a device, and it is said rather than papered over with a
         * number. */
        expect_value("nvn-buffer-address-without-a-pool-is-zero", address_one, 0);
        expect_condition("nvn-buffer-map-without-a-pool-is-null", buffer_map(buffer_one) == NULL);
        available = 0;
        resolved = nvn_memory_pool_resolve_address(address_one, &available);
        expect_condition("nvn-buffer-an-address-of-no-pool-is-not-resolved", resolved == NULL);
    }

    /* 5. The guest's own objects were not written to. The layout of a buffer
     *    and of a builder is a property of the Nintendo SDK, so the runtime
     *    keeps its state on the host. */
    expect_condition("nvn-buffer-builder-not-written", all_bytes(builder_one, sizeof(builder_one), 0xaa));
    expect_condition("nvn-buffer-object-not-written", all_bytes(buffer_one, sizeof(buffer_one), 0xaa));

    /* 6. A buffer is a snapshot of the builder it was made from. The builder
     *    changes here, and the buffer must not. */
    builder_set_storage(builder_one, pool_object, (ptrdiff_t)TEST_OTHER_OFFSET, (size_t)TEST_OTHER_SIZE);
    expect_value("nvn-buffer-snapshot-keeps-the-size", (uint64_t)buffer_get_size(buffer_one), (uint64_t)TEST_BUFFER_SIZE);
    expect_value("nvn-buffer-snapshot-keeps-the-memory-offset", (uint64_t)(int64_t)buffer_get_memory_offset(buffer_one),
                 (uint64_t)(int64_t)TEST_MEMORY_OFFSET);
    expect_value("nvn-buffer-builder-size-changed", (uint64_t)builder_get_size(builder_one), (uint64_t)TEST_OTHER_SIZE);
    expect_value("nvn-buffer-builder-memory-offset-changed", (uint64_t)(int64_t)builder_get_memory_offset(builder_one),
                 (uint64_t)(int64_t)TEST_OTHER_OFFSET);
    if (pool_exists) {
        address_two = buffer_get_address(buffer_one);
        expect_value("nvn-buffer-snapshot-keeps-the-address", address_two, address_one);
    }

    /* 7. A second buffer is made out of a second builder, and the two are
     *    not the same buffer. */
    fill_builder(builder_set_defaults, builder_set_device, builder_set_storage, builder_two, device, pool_object,
                 (ptrdiff_t)TEST_OTHER_OFFSET, (size_t)TEST_OTHER_SIZE);
    expect_condition("nvn-buffer-initialize-succeeds-again", buffer_initialize(buffer_two, builder_two) == 1);
    expect_value("nvn-buffer-second-size-round-trips", (uint64_t)buffer_get_size(buffer_two), (uint64_t)TEST_OTHER_SIZE);
    expect_value("nvn-buffer-second-memory-offset-round-trips", (uint64_t)(int64_t)buffer_get_memory_offset(buffer_two),
                 (uint64_t)(int64_t)TEST_OTHER_OFFSET);
    if (pool_exists) {
        address_two = buffer_get_address(buffer_two);
        expect_condition("nvn-buffer-second-address-differs", address_two != address_one);
        expect_value("nvn-buffer-second-address-is-the-pool-memory-plus-the-offset", address_two,
                     (uint64_t)(uintptr_t)pool_storage + (uint64_t)TEST_OTHER_OFFSET);
    }

    /* 8. The flush and the invalidate are answered and change nothing. */
    buffer_flush_mapped_range(buffer_one, (ptrdiff_t)TEST_MEMORY_OFFSET, (size_t)TEST_BUFFER_SIZE);
    buffer_invalidate_mapped_range(buffer_one, (ptrdiff_t)TEST_MEMORY_OFFSET, (size_t)TEST_BUFFER_SIZE);
    expect_value("nvn-buffer-the-flush-changed-nothing", (uint64_t)buffer_get_size(buffer_one),
                 (uint64_t)TEST_BUFFER_SIZE);

    /* 9. The refusals. A builder that was never prepared has no storage, and
     *    a buffer that was never initialized does not exist. */
    expect_condition("nvn-buffer-initialize-refuses-an-unprepared-builder",
                     buffer_initialize(buffer_unknown, builder_unprepared) == 0);
    expect_value("nvn-buffer-size-of-an-unknown-buffer-is-zero", (uint64_t)buffer_get_size(buffer_unknown), 0);
    expect_value("nvn-buffer-address-of-an-unknown-buffer-is-zero", (uint64_t)buffer_get_address(buffer_unknown), 0);
    expect_condition("nvn-buffer-map-of-an-unknown-buffer-is-null", buffer_map(buffer_unknown) == NULL);
    expect_value("nvn-buffer-debug-id-of-an-unknown-buffer-is-zero", (uint64_t)buffer_get_debug_id(buffer_unknown), 0);

    /* 10. The debug id of a buffer that exists is not zero, and two buffers
     *     do not share one. */
    debug_id_one = buffer_get_debug_id(buffer_one);
    debug_id_two = buffer_get_debug_id(buffer_two);
    expect_condition("nvn-buffer-debug-id-is-not-zero", debug_id_one != 0);
    expect_condition("nvn-buffer-debug-ids-are-per-buffer", debug_id_one != debug_id_two);

    /* 11. Finalizing a buffer takes it out of the runtime's table. */
    buffer_finalize(buffer_two);
    expect_value("nvn-buffer-size-of-a-finalized-buffer-is-zero", (uint64_t)buffer_get_size(buffer_two), 0);
    expect_value("nvn-buffer-address-of-a-finalized-buffer-is-zero", (uint64_t)buffer_get_address(buffer_two), 0);

    /* 12. The guest's objects are still untouched after all of that. */
    expect_condition("nvn-buffer-builder-not-written-afterwards",
                     all_bytes(builder_one, sizeof(builder_one), 0xaa) &&
                         all_bytes(builder_two, sizeof(builder_two), 0xaa) &&
                         all_bytes(builder_unprepared, sizeof(builder_unprepared), 0xaa));
    expect_condition("nvn-buffer-object-not-written-afterwards",
                     all_bytes(buffer_one, sizeof(buffer_one), 0xaa) &&
                         all_bytes(buffer_two, sizeof(buffer_two), 0xaa) &&
                         all_bytes(buffer_unknown, sizeof(buffer_unknown), 0xaa));

    /* 13. The counters. */
    nvn_buffer_get_stats(&stats);
    expect_condition("nvn-buffer-stats-count-calls", stats.calls > 0);
    expect_condition("nvn-buffer-stats-count-builder-calls", stats.builder_calls > 0);
    expect_condition("nvn-buffer-stats-count-initialized", stats.initialized >= 2);
    expect_condition("nvn-buffer-stats-count-finalized", stats.finalized >= 1);
    expect_condition("nvn-buffer-stats-count-gets", stats.gets > 0);
    expect_condition("nvn-buffer-stats-count-maps", stats.maps > 0);
    expect_condition("nvn-buffer-stats-count-flushes", stats.flushes >= 1);
    expect_condition("nvn-buffer-stats-count-invalidates", stats.invalidates >= 1);
    expect_condition("nvn-buffer-stats-count-refusals", stats.refused > 0);
    expect_condition("nvn-buffer-stats-have-live-buffers", stats.live_buffers >= 1);
    expect_condition("nvn-buffer-stats-have-live-builders", stats.live_builders >= 1);

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    CHECK(!errors);
    printf("{\"nvn_buffer_errors\":%u,\"names\":%zu,\"initialized\":%llu,\"finalized\":%llu,"
           "\"gets\":%llu,\"maps\":%llu,\"flushes\":%llu,\"invalidates\":%llu,\"refused\":%llu,"
           "\"live_buffers\":%d,\"live_builders\":%d}\n",
           errors, count, (unsigned long long)stats.initialized, (unsigned long long)stats.finalized,
           (unsigned long long)stats.gets, (unsigned long long)stats.maps, (unsigned long long)stats.flushes,
           (unsigned long long)stats.invalidates, (unsigned long long)stats.refused, stats.live_buffers,
           stats.live_builders);
    return 0;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-buffer")) {
        fprintf(stderr,"usage: %s --nvn-buffer\n",argv[0]);
        return 2;
    }
    return nvn_buffer_contract();
}
