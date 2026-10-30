/* A small SHA-256 for the FFRS RG477M native runtime.
 *
 * The shader key is the digest of a shader's control section and its code
 * area, so that the same shader always gets the same name, no matter which
 * run or which host produced it. The runtime has to compute the same digest
 * as the offline translator, or the two would never agree on a shader.
 *
 * The implementation is here and not a library: the build host and the device
 * have no common crypto library, and this is the one place the runtime needs
 * a digest.
 */
#ifndef FFRS_NVN_SHA256_H
#define FFRS_NVN_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define NVN_SHA256_DIGEST_SIZE 32

/* The length of the key the runtime writes down: the first eight bytes of
 * the digest, as sixteen hexadecimal characters. That is the key the other
 * host that runs this same game uses, so the two can be compared. */
#define NVN_SHA256_KEY_SIZE 8
#define NVN_SHA256_KEY_HEX_SIZE 17

typedef struct nvn_sha256_context {
    uint32_t state[8];
    uint64_t length;
    unsigned char buffer[64];
    size_t pending;
} nvn_sha256_context;

void nvn_sha256_init(nvn_sha256_context *ctx);
void nvn_sha256_update(nvn_sha256_context *ctx, const void *data, size_t size);
void nvn_sha256_final(nvn_sha256_context *ctx, unsigned char digest[NVN_SHA256_DIGEST_SIZE]);
void nvn_sha256(const void *data, size_t size, unsigned char digest[NVN_SHA256_DIGEST_SIZE]);

/* The key as sixteen lowercase hexadecimal characters, the way the shader keys
 * are written down. */
void nvn_sha256_hex(const unsigned char digest[NVN_SHA256_DIGEST_SIZE], char out[NVN_SHA256_KEY_HEX_SIZE]);

#endif /* FFRS_NVN_SHA256_H */
