/* gzip.h - minimal RFC 1952 (gzip) container + RFC 1951 (DEFLATE) codec.
 *
 * WHY THIS EXISTS: the engine had no compression code at all, so a `.vverse`
 * package - whose body is `gzip(JSON)` per the reference implementation
 * `tools/vverse_pack.js` - could not be produced (nor read) by the engine.
 * This file is deliberately standalone: no zlib, no platform code.
 *
 * Two directions, with different requirements:
 *
 *   gzip_pack_stored()  ENGINE -> PACKAGE.  Stored (BTYPE=00) blocks only.
 *       A stored block IS valid DEFLATE and the whole thing is a valid gzip
 *       member, so zlib/Node/Python all decode it.  We deliberately do not
 *       implement a compressor: the hard requirement on this side is
 *       DETERMINISM (pack the same directory twice -> byte-identical output,
 *       mtime field pinned to 0, fixed OS byte, no dictionary/timestamp
 *       dependence), and stored blocks give that for free.
 *
 *   gzip_unpack()       PACKAGE -> ENGINE.  A full decoder - stored, fixed
 *       Huffman and dynamic Huffman - because packages produced by
 *       `tools/vverse_pack.js` use zlib's real compressor (level 6, dynamic
 *       Huffman).  Header/trailer are validated: CRC32 and ISIZE.
 *
 * Both functions return 0 on success, -1 on failure; the decoder writes a
 * short reason into err.  Caller frees *out with free().
 */
#ifndef INIMERSE_COMMON_GZIP_H
#define INIMERSE_COMMON_GZIP_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32 as used by gzip (reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF). */
uint32_t gzip_crc32(const void *data, size_t len);

/* Deterministic gzip member around `len` raw bytes. */
int gzip_pack_stored(const void *data, size_t len, unsigned char **out, size_t *outlen);

/* Decode one gzip member.  Rejects a bad header, a truncated stream, a bad
 * DEFLATE block, a CRC32 mismatch and an ISIZE mismatch. */
int gzip_unpack(const void *data, size_t len, unsigned char **out, size_t *outlen,
                char *err, size_t errlen);

#endif /* INIMERSE_COMMON_GZIP_H */
