/* Execute the supplied SDK's allowlisted DSP routines natively, with the existing
 * Android x18/SCS boundary. No copied game bytes are stored in source. No
 * decoder, interpreter, JIT, SDK init, or generic export passthrough exists. */
#include "sdk_component.h"
#include "../nso/nso_loader.h"
#include "../nso/native_boundary.h"
#include "../../graphics/nvn_device/nvn_sha256.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nso_module *sdk;
static int ready;
static pthread_mutex_t load_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct { size_t pending, charged; } image_owner;
static image_owner owner;
static struct nso_result result(enum nso_status status, int error) {
    return (struct nso_result){status, error, 0};
}
static struct nso_result reserve(void *context, size_t bytes, void **ticket) {
    image_owner *o = context;
    if (!bytes || bytes > 16u*1024u*1024u || o->pending || o->charged) return result(NSO_LIMIT, ENOMEM);
    o->pending = bytes; *ticket = o; return result(NSO_OK, 0);
}
static struct nso_result commit(void *context, void *ticket) {
    image_owner *o = context;
    if (ticket != o || !o->pending || o->charged) return result(NSO_OWNER_ERROR, EINVAL);
    o->charged = o->pending; o->pending = 0; return result(NSO_OK, 0);
}
static struct nso_result cancel(void *context, void *ticket) {
    image_owner *o = context;
    if (ticket != o || o->charged) return result(NSO_OWNER_ERROR, EINVAL);
    o->pending = 0; return result(NSO_OK, 0);
}
static struct nso_result release(void *context, void *ticket) {
    image_owner *o = context;
    if (ticket != o || !o->charged) return result(NSO_OWNER_ERROR, EINVAL);
    o->charged = 0; return result(NSO_OK, 0);
}
static const struct nso_owner_ops owner_ops = {reserve, commit, cancel, release};

int ffrs_sdk_component_load(const char *path) {
    static const unsigned char expected[32] = {
        0x9d,0xfd,0x1c,0xc6,0xd3,0x20,0x9d,0x85,0x7d,0xca,0xfb,0x19,0x17,0x36,0x26,0x5a,
        0xc8,0x12,0x40,0x1d,0xdc,0xc7,0xb4,0x9d,0xab,0xf3,0xbc,0x0b,0x11,0x46,0x55,0xa9};
    pthread_mutex_lock(&load_lock);
    if (sdk) { pthread_mutex_unlock(&load_lock); return ready ? 0 : -1; }
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) { pthread_mutex_unlock(&load_lock); return -1; }
    unsigned char buffer[65536], digest[32]; size_t bytes;
    nvn_sha256_context hash; nvn_sha256_init(&hash);
    while ((bytes = fread(buffer, 1, sizeof(buffer), file))) nvn_sha256_update(&hash, buffer, bytes);
    int failed = ferror(file); fclose(file); nvn_sha256_final(&hash, digest);
    if (failed || memcmp(digest, expected, sizeof(digest))) {
        fprintf(stderr, "[sdk-component] refused: SDK SHA256 differs from the verified input\n");
        pthread_mutex_unlock(&load_lock); return -1;
    }
    char error[256] = {0};
    /* No external import is resolved. Unexpected allocation, startup or
     * foreign module calls stay fatal sentinels, never host fallthrough. */
    struct nso_load_result loaded = nso_load_file_owned(path, NULL, &owner_ops, &owner, error, sizeof(error));
    sdk = loaded.module; /* Retain the owner even if loader cleanup failed. */
    if (loaded.result.status != NSO_OK) {
        fprintf(stderr, "[sdk-component] load failed status=%d error=%s\n", loaded.result.status, error);
        pthread_mutex_unlock(&load_lock); return -1;
    }
    uint32_t symbol = nso_find_symbol(sdk, "_ZN2nn3hid18VibrationConverter13ChunkDurationE");
    if (!symbol || nso_symbol_value(sdk, symbol) != 0x9a05a8 ||
        *(const int64_t *)(uintptr_t)nso_addr(sdk, 0x9a05a8) != 5000000) {
        fprintf(stderr, "[sdk-component] verified data symbol does not match loaded SDK\n");
        pthread_mutex_unlock(&load_lock); return -1;
    }
    fprintf(stderr, "[sdk-component] verified native DSP image base=%p image_bytes=%zu unresolved=%u; no SDK init\n",
            (void *)(uintptr_t)nso_base(sdk), owner.charged, nso_unresolved_count(sdk));
    __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&load_lock); return 0;
}

static void invoke(uint64_t offset, nb_registers *packet) {
    uint64_t address = 0;
    nb_scope scope = {0};
    if (!sdk || nso_entry_address(sdk, offset, &address).status != NSO_OK ||
        nb_scope_init(&scope) != NB_OK) abort();
    nb_status status = nb_call(&scope, (void (*)(void))(uintptr_t)address, packet);
    if (status != NB_OK) {
        fprintf(stderr, "[sdk-component] native boundary failed offset=%#llx status=%d\n", (unsigned long long)offset, status);
        abort();
    }
    if (nb_scope_close(&scope) != NB_OK) abort();
}
static void converter_ctor(void *self, int rate) {
    nb_registers packet = {.x={(uintptr_t)self, (uint32_t)rate}};
    invoke(0x517e00, &packet);
    fprintf(stderr, "[sdk-component] original VibrationConverter constructed rate=%d\n", rate);
}
static void converter_process(void *self, void *out, const int16_t *pcm, int count) {
    nb_registers packet = {.x={(uintptr_t)self, (uintptr_t)out, (uintptr_t)pcm, (uint32_t)count}};
    invoke(0x518030, &packet);
}
static void converter_dtor(void *self) {
    nb_registers packet = {.x={(uintptr_t)self}};
    invoke(0x518008, &packet);
}
/* Same verified SDK: these four DSP-only functions use caller-owned state
 * and work memory. Their internal memset/FIR calls stay within this image. */
static uint64_t resampler_size(int channels) {
    nb_registers p={.x={(uint32_t)channels}}; invoke(0x2634e0,&p); return p.x[0];
}
static int resampler_count(const void *state,int frames) {
    nb_registers p={.x={(uintptr_t)state,(uint32_t)frames}};invoke(0x2634f0,&p);return (int)p.x[0];
}
static uint32_t resampler_init(void *state,void *work,uint64_t bytes,int input_rate,int output_rate,int channels) {
    nb_registers p={.x={(uintptr_t)state,(uintptr_t)work,bytes,(uint32_t)input_rate,(uint32_t)output_rate,(uint32_t)channels}};
    invoke(0x263530,&p);return (uint32_t)p.x[0];
}
static uint32_t resampler_process(void *state,int *count,int16_t *output,uint64_t capacity,const int16_t *input,int frames) {
    nb_registers p={.x={(uintptr_t)state,(uintptr_t)count,(uintptr_t)output,capacity,(uintptr_t)input,(uint32_t)frames}};
    invoke(0x263690,&p);return (uint32_t)p.x[0];
}
uint64_t ffrs_sdk_component_lookup(const char *name, unsigned *arguments, unsigned *returns) {
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE) || !name) return 0;
    uint64_t address = 0; unsigned args = 0,rets=0;
    if (!strcmp(name, "_ZN2nn3hid18VibrationConverter13ChunkDurationE")) address = nso_addr(sdk, 0x9a05a8);
    else if (!strcmp(name, "_ZN2nn3hid18VibrationConverterC1Ei")) { address=(uintptr_t)converter_ctor; args=2; }
    else if (!strcmp(name, "_ZN2nn3hid18VibrationConverterD1Ev")) { address=(uintptr_t)converter_dtor; args=1; }
    else if (!strcmp(name, "_ZN2nn3hid18VibrationConverter12ProcessChunkEPNS0_14VibrationValueEPKsi")) {
        address=(uintptr_t)converter_process; args=4;
    }
    else if(!strcmp(name,"_ZN2nn5audio33GetRequiredBufferSizeForResamplerEi")){address=(uintptr_t)resampler_size;args=1;rets=1;}
    else if(!strcmp(name,"_ZN2nn5audio29GetResamplerOutputSampleCountEPKNS0_13ResamplerTypeEi")){address=(uintptr_t)resampler_count;args=2;rets=1;}
    else if(!strcmp(name,"_ZN2nn5audio19InitializeResamplerEPNS0_13ResamplerTypeEPvmiii")){address=(uintptr_t)resampler_init;args=6;rets=1;}
    else if(!strcmp(name,"_ZN2nn5audio22ProcessResamplerBufferEPNS0_13ResamplerTypeEPiPsmPKsi")){address=(uintptr_t)resampler_process;args=6;rets=1;}
    if (address) { if (arguments) *arguments=args; if (returns) *returns=rets; }
    return address;
}
