/* checksum.c - file checksums for incremental compilation */
#include "checksum.h"
#include "../common/sha256.h"
#include <stdio.h>
#include <string.h>

int inim_file_sha256(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f) { memset(out, '0', 65); return -1; }
    Sha256Ctx ctx;
    sha256_init(&ctx);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha256_update(&ctx, buf, n);
    int err = ferror(f);
    fclose(f);
    if (err) { memset(out, '0', 65); return -1; }
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    sha256_hex_of_digest(digest, out);
    return 0;
}

void inim_calculate_bytecode_checksum(const char *bytecode_path, char out[65]) {
    if (inim_file_sha256(bytecode_path, out) != 0) memset(out, '0', 65);
}

void inim_bytecode_obj_checksum(const char *obj_path, char out[65]) {
    if (inim_file_sha256(obj_path, out) != 0) memset(out, '0', 65);
}

void inim_dll_checksum(const char *dll_path, char out[65]) {
    if (inim_file_sha256(dll_path, out) != 0) memset(out, '0', 65);
}
