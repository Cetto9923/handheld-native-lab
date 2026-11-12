/* Regression contract for the texture family's immutable getter-address cache.
 * Each mode runs in a separate process so pthread_once starts uninitialized.
 * The production implementation is exercised with immutable mock family tables.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "../../src/graphics/nvn_device/nvn_texture.c"

enum {
    READER_COUNT = 8,
    SNAPSHOTS_PER_READER = 500,
    BUILDER_GETTER_COUNT = 13
};

static atomic_uint builder_resolution_calls;
static atomic_uint pool_resolution_calls;
static atomic_uint value_getter_calls;
static int missing_builder_getter;
static int missing_pool_getter;
static unsigned retired_texture_count;
static uint64_t expected_retired_serial;
static pthread_mutex_t reader_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reader_start_condition = PTHREAD_COND_INITIALIZER;
static unsigned ready_readers;

struct MockBuilder {
    int value;
};

static int mock_get_integer(void *builder) {
    atomic_fetch_add(&value_getter_calls, 1);
    return ((struct MockBuilder *)builder)->value;
}

static ptrdiff_t mock_get_offset(void *builder) {
    atomic_fetch_add(&value_getter_calls, 1);
    return ((struct MockBuilder *)builder)->value + 64;
}

static void *mock_get_pointer(void *builder) {
    atomic_fetch_add(&value_getter_calls, 1);
    return builder;
}

static void mock_get_swizzle(void *builder, NVNtextureSwizzle *red,
                             NVNtextureSwizzle *green, NVNtextureSwizzle *blue,
                             NVNtextureSwizzle *alpha) {
    (void)builder;
    atomic_fetch_add(&value_getter_calls, 1);
    *red = NVN_TEXTURE_SWIZZLE_R;
    *green = NVN_TEXTURE_SWIZZLE_G;
    *blue = NVN_TEXTURE_SWIZZLE_B;
    *alpha = NVN_TEXTURE_SWIZZLE_A;
}

static uint64_t mock_get_address(const void *pool) {
    return (uint64_t)(uintptr_t)pool;
}

#define INTEGER_GETTER(name) \
    { "nvnTextureBuilderGet" name, (void *)(uintptr_t)mock_get_integer }
static const nvn_texture_builder_family_entry mock_builder_family[] = {
    INTEGER_GETTER("Format"),
    INTEGER_GETTER("Target"),
    INTEGER_GETTER("Flags"),
    INTEGER_GETTER("Width"),
    INTEGER_GETTER("Height"),
    INTEGER_GETTER("Depth"),
    INTEGER_GETTER("Levels"),
    INTEGER_GETTER("Samples"),
    INTEGER_GETTER("DepthStencilMode"),
    { "nvnTextureBuilderGetStride", (void *)(uintptr_t)mock_get_offset },
    { "nvnTextureBuilderGetMemoryOffset", (void *)(uintptr_t)mock_get_offset },
    { "nvnTextureBuilderGetMemoryPool", (void *)(uintptr_t)mock_get_pointer },
    { "nvnTextureBuilderGetSwizzle", (void *)(uintptr_t)mock_get_swizzle }
};
#undef INTEGER_GETTER

static const nvn_memory_pool_family_entry mock_pool_family[] = {
    { "nvnMemoryPoolGetBufferAddress", (void *)(uintptr_t)mock_get_address }
};

size_t nvn_texture_builder_family_count(void) {
    atomic_fetch_add(&builder_resolution_calls, 1);
    return sizeof(mock_builder_family) / sizeof(mock_builder_family[0]);
}

const nvn_texture_builder_family_entry *nvn_texture_builder_family_entry_at(size_t index) {
    if (missing_builder_getter && index == 4) {
        return NULL;
    }
    assert(index < BUILDER_GETTER_COUNT);
    return &mock_builder_family[index];
}

size_t nvn_memory_pool_family_count(void) {
    atomic_fetch_add(&pool_resolution_calls, 1);
    return sizeof(mock_pool_family) / sizeof(mock_pool_family[0]);
}

const nvn_memory_pool_family_entry *nvn_memory_pool_family_entry_at(size_t index) {
    assert(index == 0);
    if (missing_pool_getter) {
        return NULL;
    }
    return &mock_pool_family[index];
}

void *nvn_texture_builder_device_of(const void *builder) {
    return (void *)builder;
}

int nvn_texture_builder_layout_of(const void *builder, uint64_t *size,
                                  uint32_t *alignment, int *block_height) {
    (void)builder;
    if (size) {
        *size = 4096;
    }
    if (alignment) {
        *alignment = 256;
    }
    if (block_height) {
        *block_height = 1;
    }
    return 1;
}

uint32_t nvn_device_generate_debug_domain_id(void *device, const char *label) {
    (void)device;
    (void)label;
    return 17;
}

void nvn_bridge_texture_end(const void *texture, uint64_t serial) {
    assert(texture);
    assert(serial == expected_retired_serial);
    ++retired_texture_count;
}

static void *read_builder_snapshots(void *argument) {
    struct MockBuilder builder = { 1 };
    unsigned reader_value = (unsigned)(uintptr_t)argument;

    /* Release all readers together before the first function resolution. */
    pthread_mutex_lock(&reader_start_lock);
    ++ready_readers;
    if (ready_readers == READER_COUNT) {
        pthread_cond_broadcast(&reader_start_condition);
    }
    while (ready_readers < READER_COUNT) {
        pthread_cond_wait(&reader_start_condition, &reader_start_lock);
    }
    pthread_mutex_unlock(&reader_start_lock);

    for (unsigned iteration = 0; iteration < SNAPSHOTS_PER_READER; ++iteration) {
        nvn_texture_state texture = { 0 };
        builder.value = (int)(reader_value + iteration + 1);
        int copied = snapshot_builder(&texture, &builder);
        assert(copied == !missing_builder_getter);
        if (copied) {
            assert(texture.format == (NVNformat)builder.value);
            assert(texture.target == (NVNtextureTarget)builder.value);
            assert(texture.width == builder.value);
            assert(texture.height == builder.value);
            assert(texture.flags == (unsigned)builder.value);
            assert(texture.depth == builder.value);
            assert(texture.levels == builder.value);
            assert(texture.samples == builder.value);
            assert(texture.stride == builder.value + 64);
            assert(texture.memory_offset == builder.value + 64);
            assert(texture.memory_pool == &builder);
            assert(texture.device == &builder);
            assert(texture.depth_stencil_mode == (NVNtextureDepthStencilMode)builder.value);
            assert(texture.swizzle[0] == NVN_TEXTURE_SWIZZLE_R);
            assert(texture.swizzle[1] == NVN_TEXTURE_SWIZZLE_G);
            assert(texture.swizzle[2] == NVN_TEXTURE_SWIZZLE_B);
            assert(texture.swizzle[3] == NVN_TEXTURE_SWIZZLE_A);
        }
        uint64_t expected_address = missing_pool_getter ? 0 : (uint64_t)(uintptr_t)&builder;
        assert(memory_pool_buffer_address(&builder) == expected_address);
        assert(memory_pool_buffer_address(NULL) == 0);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "missing-builder")) {
        missing_builder_getter = 1;
    } else if (argc == 2 && !strcmp(argv[1], "missing-pool")) {
        missing_pool_getter = 1;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [missing-builder|missing-pool]\n", argv[0]);
        return 2;
    }

    pthread_t readers[READER_COUNT];
    for (unsigned reader = 0; reader < READER_COUNT; ++reader) {
        int result = pthread_create(&readers[reader], NULL, read_builder_snapshots,
                                    (void *)(uintptr_t)(reader * 1000));
        assert(result == 0);
    }
    for (unsigned reader = 0; reader < READER_COUNT; ++reader) {
        assert(pthread_join(readers[reader], NULL) == 0);
    }
    assert(atomic_load(&builder_resolution_calls) == BUILDER_GETTER_COUNT);
    assert(atomic_load(&pool_resolution_calls) == 1);
    unsigned expected_getter_calls = missing_builder_getter ? 0 :
        READER_COUNT * SNAPSHOTS_PER_READER * BUILDER_GETTER_COUNT;
    assert(atomic_load(&value_getter_calls) == expected_getter_calls);

    struct MockBuilder builder = { 7 };
    unsigned char texture_object;
    assert(nvnTextureInitialize(&texture_object, &builder) == !missing_builder_getter);
    if (!missing_builder_getter) {
        builder.value = 29;
        pthread_mutex_lock(&s_texture_lock);
        nvn_texture_state *stored = texture_find(&texture_object, 0);
        assert(stored);
        assert(stored->width == 7);
        assert(stored->stride == 71);
        assert(stored->packaged_block_height == 1);
        expected_retired_serial = stored->serial;
        pthread_mutex_unlock(&s_texture_lock);

        assert(nvnTextureInitialize(&texture_object, &builder));
        pthread_mutex_lock(&s_texture_lock);
        stored = texture_find(&texture_object, 0);
        assert(stored);
        assert(stored->width == 29);
        assert(stored->serial != expected_retired_serial);
        pthread_mutex_unlock(&s_texture_lock);
    }
    assert(atomic_load(&builder_resolution_calls) == BUILDER_GETTER_COUNT);
    assert(atomic_load(&pool_resolution_calls) == 1);
    assert(retired_texture_count == (missing_builder_getter ? 0 : 1));
    puts("Texture family lookup cache contract PASS");
    return 0;
}
