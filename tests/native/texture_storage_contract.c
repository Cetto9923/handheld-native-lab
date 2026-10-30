/* Fixed layout fixtures test the production builder and initialized texture,
 * rather than deriving expected allocation bytes from the implementation. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../../src/graphics/nvn_device/nvn_texture_builder.h"

void nvnTextureBuilderSetDefaults(void *);
void nvnTextureBuilderSetFormat(void *, int);
void nvnTextureBuilderSetSize2D(void *, int, int);
void nvnTextureBuilderSetTarget(void *, int);
size_t nvnTextureBuilderGetStorageSize(void *);
size_t nvnTextureBuilderGetStorageAlignment(void *);
int nvnTextureInitialize(void *, const void *);
int nvnTextureGetStorageSize(const void *);
uint32_t nvn_device_generate_debug_domain_id(void *d, const char *s) { (void)d; (void)s; return 1; }
uint64_t nvn_memory_pool_gpu_address(const void *p) { (void)p; return 0; }

static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)
int main(void) {
    _Alignas(16) unsigned char builder[128] = {0}, copied[128], texture[192] = {0};
    nvnTextureBuilderSetDefaults(builder);
    nvnTextureBuilderSetFormat(builder, 0x25); /* RGBA8 */
    nvnTextureBuilderSetSize2D(builder, 1280, 720);
    CHECK(nvnTextureBuilderGetStorageSize(builder) == 3932160);
    CHECK(nvnTextureBuilderGetStorageAlignment(builder) == 512);
    memcpy(copied, builder, sizeof(copied));
    CHECK(nvnTextureBuilderGetStorageSize(copied) == 3932160);
    CHECK(nvnTextureInitialize(texture, copied));
    CHECK(nvnTextureGetStorageSize(texture) == 3932160);
    nvnTextureBuilderSetSize2D(builder, 1024, 1024);
    CHECK(nvnTextureBuilderGetStorageSize(builder) == 4194304);
    CHECK(nvnTextureBuilderGetStorageSize(copied) == 3932160);
    CHECK(nvnTextureGetStorageSize(texture) == 3932160);
    nvnTextureBuilderSetFormat(builder, 0x41); /* BC1: one 4x4 block in a GOB. */
    nvnTextureBuilderSetSize2D(builder, 4, 4);
    CHECK(nvnTextureBuilderGetStorageSize(builder) == 512);
    nvnTextureBuilderSetFormat(builder, 0x25);
    nvnTextureBuilderSetTarget(builder, 10); /* Buffer: four 4-byte elements. */
    CHECK(nvnTextureBuilderGetStorageSize(builder) == 16);
    CHECK(nvnTextureBuilderGetStorageAlignment(builder) == 4);
    nvnTextureBuilderSetFormat(builder, 0x7fff);
    CHECK(nvnTextureBuilderGetStorageSize(builder) == 0);
    CHECK(!nvnTextureInitialize(texture, builder));
    printf("texture_storage_contract errors=%d\n", errors);
    return errors ? 1 : 0;
}
