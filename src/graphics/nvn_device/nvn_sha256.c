/* The SHA-256 implementation. See nvn_sha256.h for why it is here. */
#include "nvn_sha256.h"

#include <string.h>

static const uint32_t s_round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u
};

static uint32_t rotr(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void transform(nvn_sha256_context *ctx, const unsigned char block[64]) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice + s_round_constants[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void nvn_sha256_init(nvn_sha256_context *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->length = 0;
    ctx->pending = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void nvn_sha256_update(nvn_sha256_context *ctx, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    if (!ctx || !bytes) return;
    ctx->length += (uint64_t)size;
    while (size > 0) {
        size_t take = 64 - ctx->pending;
        if (take > size) take = size;
        memcpy(ctx->buffer + ctx->pending, bytes, take);
        ctx->pending += take;
        bytes += take;
        size -= take;
        if (ctx->pending == 64) {
            transform(ctx, ctx->buffer);
            ctx->pending = 0;
        }
    }
}

void nvn_sha256_final(nvn_sha256_context *ctx, unsigned char digest[NVN_SHA256_DIGEST_SIZE]) {
    uint64_t bits;
    unsigned char padding[72];
    size_t padding_size;
    int i;
    if (!ctx || !digest) return;
    bits = ctx->length * 8;
    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80;
    /* The length goes into the last eight bytes, so the padding is one block
     * at most, plus the eight bytes themselves. */
    padding_size = (ctx->pending < 56) ? (56 - ctx->pending) : (120 - ctx->pending);
    nvn_sha256_update(ctx, padding, padding_size);
    for (i = 0; i < 8; i++) {
        unsigned char byte = (unsigned char)((bits >> (56 - 8 * i)) & 0xff);
        nvn_sha256_update(ctx, &byte, 1);
    }
    for (i = 0; i < 8; i++) {
        digest[4 * i] = (unsigned char)((ctx->state[i] >> 24) & 0xff);
        digest[4 * i + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xff);
        digest[4 * i + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xff);
        digest[4 * i + 3] = (unsigned char)(ctx->state[i] & 0xff);
    }
}

void nvn_sha256(const void *data, size_t size, unsigned char digest[NVN_SHA256_DIGEST_SIZE]) {
    nvn_sha256_context ctx;
    nvn_sha256_init(&ctx);
    nvn_sha256_update(&ctx, data, size);
    nvn_sha256_final(&ctx, digest);
}

void nvn_sha256_hex(const unsigned char digest[NVN_SHA256_DIGEST_SIZE], char out[NVN_SHA256_KEY_HEX_SIZE]) {
    static const char hex[] = "0123456789abcdef";
    int i;
    if (!digest || !out) return;
    for (i = 0; i < NVN_SHA256_KEY_SIZE; i++) {
        out[2 * i] = hex[(digest[i] >> 4) & 0xf];
        out[2 * i + 1] = hex[digest[i] & 0xf];
    }
    out[2 * NVN_SHA256_KEY_SIZE] = '\0';
}
