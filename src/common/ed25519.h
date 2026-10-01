/* ed25519.h - compact RFC8032 Ed25519 (sign/verify), no deps
 * 32-byte seed -> 32-byte public key; 64-byte signatures.
 * Internal SHA-512 implementation. All inputs/outputs are raw bytes. */
#ifndef ED25519_H
#define ED25519_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* derive public key from 32-byte seed; pub must hold 32 bytes */
void ed25519_pubkey(const unsigned char seed[32], unsigned char pub[32]);
/* sign msg with seed; sig must hold 64 bytes */
void ed25519_sign(const unsigned char seed[32],
                  const unsigned char *msg, size_t msglen,
                  unsigned char sig[64]);
/* verify; returns 1 on success, 0 on failure */
int ed25519_verify(const unsigned char pub[32],
                   const unsigned char *msg, size_t msglen,
                   const unsigned char sig[64]);
/* SHA-512.  One-shot, plus a streaming interface: the streaming form is what
 * ed25519_sign/ed25519_verify use, so that arbitrarily long messages hash
 * correctly.  (They used to copy R||A||M into a fixed 8320-byte buffer and
 * silently stop copying once it filled, producing signatures that no other
 * RFC 8032 implementation would accept for messages over ~8 KiB.) */
void sha512_buf(const unsigned char *data, size_t len, unsigned char out[64]);

typedef struct {
    uint64_t h[8];
    uint64_t total;        /* bytes consumed so far */
    unsigned char buf[128]; /* partial block */
    size_t buflen;
} Sha512Ctx;

void sha512_init(Sha512Ctx *c);
void sha512_update(Sha512Ctx *c, const void *data, size_t len);
void sha512_final(Sha512Ctx *c, unsigned char out[64]);
#ifdef __cplusplus
}
#endif
#endif
