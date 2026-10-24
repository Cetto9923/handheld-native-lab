/* The runtime's own shader name, computed on real shader bytes.
 *
 * The runtime identifies a shader by hashing its control section together
 * with its code area. The offline tooling hashes the same bytes in Python.
 * If the two implementations ever disagree, the runtime would look for a
 * shader that the translator never produced, and the failure would look like
 * a missing shader rather than a wrong digest.
 *
 * This tool closes that gap: it reads the real bytes of a shader out of the
 * game's container and prints the names the runtime computes for them, so the
 * two can be compared on one sample.
 *
 * Usage: nvn_sha256_key <control.bin> <code.bin>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvn_sha256.h"

#define CONTROL_SIZE 0x880
#define CONTROL_LENGTH_OFFSET 0x20
#define CONTROL_CODE_LENGTH_OFFSET 0x704

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    unsigned char *data = malloc((size_t)length ? (size_t)length : 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (length && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static uint32_t read_u32(const unsigned char *data, size_t offset) {
    if (offset + 4 > 0) {
        uint32_t value;
        memcpy(&value, data + offset, 4);
        return value;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <control.bin> <code.bin>\n", argv[0]);
        return 2;
    }
    size_t control_size = 0;
    size_t code_size = 0;
    unsigned char *control = read_file(argv[1], &control_size);
    unsigned char *code = read_file(argv[2], &code_size);
    if (!control || !code) {
        free(control);
        free(code);
        return 2;
    }
    if (control_size < CONTROL_SIZE) {
        fprintf(stderr, "control section is %zu bytes, %d needed\n", control_size, CONTROL_SIZE);
        free(control);
        free(code);
        return 2;
    }

    uint32_t own = read_u32(control, CONTROL_LENGTH_OFFSET);
    uint32_t control_length = CONTROL_SIZE;
    if (own >= 0x760 && own < CONTROL_SIZE) {
        control_length = own;
    }
    uint32_t code_length = read_u32(control, CONTROL_CODE_LENGTH_OFFSET);
    if (code_length > code_size) {
        code_length = (uint32_t)code_size;
    }

    nvn_sha256_context content_ctx;
    nvn_sha256_init(&content_ctx);
    nvn_sha256_update(&content_ctx, control, control_length);
    nvn_sha256_update(&content_ctx, code, code_length);
    unsigned char content_digest[NVN_SHA256_DIGEST_SIZE];
    nvn_sha256_final(&content_ctx, content_digest);
    char content_key[NVN_SHA256_KEY_HEX_SIZE];
    nvn_sha256_hex(content_digest, content_key);

    unsigned char control_digest[NVN_SHA256_DIGEST_SIZE];
    nvn_sha256(control, CONTROL_SIZE, control_digest);
    char control_name[NVN_SHA256_KEY_HEX_SIZE];
    nvn_sha256_hex(control_digest, control_name);

    printf("{\"content_key\":\"%s\",\"control_key\":\"%s\","
           "\"control_length_used\":%u,\"control_length_stated\":%u,"
           "\"code_length_used\":%u,\"code_length_actual\":%zu}\n",
           content_key, control_name, control_length, own, code_length, code_size);

    free(control);
    free(code);
    return 0;
}
