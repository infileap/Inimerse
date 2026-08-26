/* sha256.h - minimal SHA-256 (FIPS 180-4), no deps, for verse hash/ref:// verification */
#ifndef INIMERSE_SHA256_H
#define INIMERSE_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t buflen;
} Sha256Ctx;

void sha256_init(Sha256Ctx *c);
void sha256_update(Sha256Ctx *c, const void *data, size_t len);
void sha256_final(Sha256Ctx *c, uint8_t out[32]);

/* one-shot: digest of data -> out[32] */
void sha256_digest(const void *data, size_t len, uint8_t out[32]);
/* one-shot hex (lowercase), out must hold 65 bytes */
void sha256_hex(const void *data, size_t len, char out[65]);
/* hex of a raw 32-byte digest, out must hold 65 bytes */
void sha256_hex_of_digest(const uint8_t digest[32], char out[65]);

#endif
