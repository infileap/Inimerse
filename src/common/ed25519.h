/* ed25519.h - compact RFC8032 Ed25519 (sign/verify), no deps
 * 32-byte seed -> 32-byte public key; 64-byte signatures.
 * Internal SHA-512 implementation. All inputs/outputs are raw bytes. */
#ifndef ED25519_H
#define ED25519_H
#include <stddef.h>
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
/* SHA-512 (also exposed for reuse) */
void sha512_buf(const unsigned char *data, size_t len, unsigned char out[64]);
#ifdef __cplusplus
}
#endif
#endif
