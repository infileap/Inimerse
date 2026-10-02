/* vverse_pack.h - engine-side `.vverse` package writer and reader.
 *
 * The format is NOT invented here: it is the one `tools/vverse_pack.js` and
 * `tools/vverse_validate.js` already implement (both are read-only reference
 * implementations, and stay the judge):
 *
 *   package = gzip(JSON)            gzip member, mtime field pinned to 0
 *   JSON    = {"format":"vverse-1","files":{ "<path>": "<base64>" , ... }}
 *   paths   = '/'-separated, relative, inside the package
 *   required dirs (strictStructure): laws/ assets/ mods/ signatures/
 *   required metadata: manifest.json, blueprint.json, signatures/sha256.json
 *
 *   signatures/sha256.json = { "<path>": "<sha256 hex of the RAW bytes>" }
 *       - `signatures/`-prefixed entries are never digest-checked (and this
 *         file never signs itself);
 *       - the reverse direction is checked just as hard: every packaged file
 *         except the two signature files must appear in the table, otherwise
 *         "unsigned file: <name>".
 *
 *   signatures/ed25519.json = {"algorithm":"ed25519","publicKey":"<b64>",
 *                              "signature":"<b64>"}   (optional)
 *       - publicKey is DER SPKI for Ed25519 (12-byte prefix + raw 32 bytes);
 *       - the signed bytes are JSON.stringify of the sha256 table with all
 *         `signatures/` entries removed, keys sorted - UTF-8 bytes;
 *       - Ed25519 hashes internally, so there is no pre-hash here.
 *
 * Two deliberate differences from the reference, both documented at the call
 * sites: (a) packing does not mutate the source directory (the JS writer drops
 * a fresh signatures/sha256.json into it), so the output is a pure function of
 * the tree it was given; (b) base64 is decoded strictly instead of zlib-style
 * leniently (see b64_decode).
 *
 * Every entry point returns 0 on success and -1 on failure, writing a short
 * reason into err (when errlen > 0).  Nothing here aborts the process.
 */
#ifndef INIMERSE_COMMON_VVERSE_PACK_H
#define INIMERSE_COMMON_VVERSE_PACK_H

#include <stddef.h>

/* Pack the directory `root` into a `.vverse` at `out_path`.
 * `seed` is an optional 32-byte Ed25519 seed: when non-NULL the package also
 * carries signatures/ed25519.json signed by it; when NULL no ed25519.json is
 * written (the reference treats it as optional). */
int vverse_pack(const char *root, const char *out_path, const unsigned char *seed,
                char *err, size_t errlen);

/* Read a `.vverse`, verify it, and extract it under `dest_dir`.
 * Refuses a wrong format, bad digests, an unsigned file, a forged/failed
 * ed25519 signature and any path that escapes `dest_dir`. */
int vverse_unpack(const char *pkg_path, const char *dest_dir, char *err, size_t errlen);

/* Directory-level validation with the reference's three switches on:
 * strictStructure + requireSignature + requireCompleteSignature, plus the
 * ed25519 check whenever signatures/ed25519.json exists. */
int vverse_validate(const char *root, char *err, size_t errlen);

/* sha256 hex (65 bytes incl. NUL) of a whole file - streaming, so there is no
 * size limit.  Used by the probe's determinism check. */
int vverse_sha256_file(const char *path, char out_hex[65], char *err, size_t errlen);

#endif /* INIMERSE_COMMON_VVERSE_PACK_H */
