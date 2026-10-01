/* SHA-256 (FIPS 180-4), incremental. Internal to the disc library. */
#ifndef DISC_SHA256_H
#define DISC_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t length;                /* bytes hashed so far */
    uint8_t block[64];
    size_t fill;                    /* bytes in block */
} Sha256;

void sha256_init(Sha256* s);
void sha256_update(Sha256* s, const void* data, size_t n);
void sha256_final(Sha256* s, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char hex[65]);

#endif
