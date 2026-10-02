/* gzip.c - RFC 1952 gzip container + RFC 1951 DEFLATE, no dependencies.
 *
 * See gzip.h for why both directions exist and why only the *decoder* is a
 * full DEFLATE implementation.  Layout of this file:
 *
 *   1. CRC-32
 *   2. writer (stored blocks, deterministic header)
 *   3. decoder: bit reader, canonical-Huffman build/decode, stored/fixed/
 *      dynamic blocks, gzip header + trailer handling
 *
 * The decoder is a from-scratch implementation of the classic bit-by-bit
 * canonical Huffman decoding scheme (count/first-code per length, as in
 * Mark Adler's public-domain puff.c): no lookup tables, so the whole thing
 * stays reviewable in one sitting, and a malformed stream can only produce an
 * error return - every length/distance/back-reference is bounds-checked
 * against the output produced so far.
 */
#include "gzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================== 1. CRC-32 ============================== */

static uint32_t gz_crc_table[256];
static int gz_crc_ready = 0;

static void gz_crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        gz_crc_table[i] = c;
    }
    gz_crc_ready = 1;
}

uint32_t gzip_crc32(const void *data, size_t len) {
    if (!gz_crc_ready) gz_crc_init();
    const unsigned char *p = (const unsigned char *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = gz_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* =============================== 2. WRITER ============================== */

static void gz_put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

int gzip_pack_stored(const void *data, size_t len, unsigned char **out, size_t *outlen) {
    if (!out || !outlen || (!data && len)) return -1;
    const unsigned char *in = (const unsigned char *)data;

    /* 10-byte header + 5 bytes per stored block + payload + 8-byte trailer.
     * One extra block covers len == 0 and exact multiples of 65535. */
    size_t blocks = len / 65535 + 1;
    size_t cap = 10 + 5 * blocks + len + 8;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return -1;
    size_t o = 0;

    buf[o++] = 0x1f;              /* ID1 */
    buf[o++] = 0x8b;              /* ID2 */
    buf[o++] = 0x08;              /* CM  = deflate */
    buf[o++] = 0x00;              /* FLG = no optional fields */
    gz_put32(buf + o, 0); o += 4; /* MTIME = 0  <- determinism requirement */
    buf[o++] = 0x00;              /* XFL  = not stated (fixed, not level-derived) */
    buf[o++] = 0x03;              /* OS   = Unix, pinned (zlib would emit the host OS) */

    size_t pos = 0;
    if (len == 0) {
        /* Empty payload still needs one final stored block with LEN = 0. */
        buf[o++] = 0x01;          /* BFINAL = 1, BTYPE = 00 (stored) */
        buf[o++] = 0x00; buf[o++] = 0x00;
        buf[o++] = 0xff; buf[o++] = 0xff;
    }
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 65535) chunk = 65535;
        int final = (pos + chunk == len);
        buf[o++] = (unsigned char)(final ? 0x01 : 0x00); /* BFINAL | BTYPE=00 */
        uint16_t l = (uint16_t)chunk;
        uint16_t nl = (uint16_t)(~l);
        buf[o++] = (unsigned char)(l & 0xff);  buf[o++] = (unsigned char)(l >> 8);
        buf[o++] = (unsigned char)(nl & 0xff); buf[o++] = (unsigned char)(nl >> 8);
        memcpy(buf + o, in + pos, chunk);
        o += chunk;
        pos += chunk;
    }

    gz_put32(buf + o, gzip_crc32(data, len)); o += 4;
    gz_put32(buf + o, (uint32_t)(len & 0xffffffffu)); o += 4;

    *out = buf;
    *outlen = o;
    return 0;
}

/* ============================== 3. DECODER ============================== */

#define GZ_MAXBITS 15

typedef struct {
    short count[GZ_MAXBITS + 1]; /* number of codes of each length */
    short symbol[288];           /* symbols ordered by code */
} GzHuff;

typedef struct {
    const unsigned char *in;
    size_t inlen, pos;
    unsigned bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t outlen, outcap;
    char *err;
    size_t errlen;
    int failed;
} GzState;

static void gz_fail(GzState *s, const char *msg) {
    if (s->failed) return;
    s->failed = 1;
    if (s->err && s->errlen) snprintf(s->err, s->errlen, "%s", msg);
}

/* Read `need` bits, LSB first. */
static int gz_bits(GzState *s, int need) {
    if (s->failed) return 0;
    while (s->bitcnt < need) {
        if (s->pos >= s->inlen) { gz_fail(s, "truncated deflate stream"); return 0; }
        s->bitbuf |= (unsigned)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    unsigned v = s->bitbuf & ((1u << need) - 1u);
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return (int)v;
}

static int gz_put(GzState *s, unsigned char b) {
    if (s->outlen == s->outcap) {
        size_t nc = s->outcap ? s->outcap * 2 : 65536;
        unsigned char *nb = (unsigned char *)realloc(s->out, nc);
        if (!nb) { gz_fail(s, "out of memory inflating package"); return -1; }
        s->out = nb;
        s->outcap = nc;
    }
    s->out[s->outlen++] = b;
    return 0;
}

/* Canonical Huffman table from code lengths.
 * Returns 0 for a complete set, >0 for an incomplete one (legal for the
 * distance table, which may have holes), -1 when over-subscribed. */
static int gz_build(GzHuff *h, const short *lengths, int n) {
    int len, left;
    for (len = 0; len <= GZ_MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) {
        if (lengths[sym] < 0 || lengths[sym] > GZ_MAXBITS) return -1;
        h->count[lengths[sym]]++;
    }
    if (h->count[0] == (short)n) return 0; /* no codes at all: caller decides */
    left = 1;
    for (len = 1; len <= GZ_MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1; /* over-subscribed */
    }
    short offs[GZ_MAXBITS + 2];
    offs[1] = 0;
    for (len = 1; len <= GZ_MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (lengths[sym]) h->symbol[offs[lengths[sym]]++] = (short)sym;
    return left;
}

static int gz_decode(GzState *s, const GzHuff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= GZ_MAXBITS; len++) {
        code |= gz_bits(s, 1);
        if (s->failed) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1; /* ran out of code space: not a code in this table */
}

/* RFC 1951 §3.2.5 */
static const short GZ_LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short GZ_LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const short GZ_DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const short GZ_DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13
};

/* Decode literal/length + distance symbols until end-of-block. */
static void gz_codes(GzState *s, const GzHuff *lencode, const GzHuff *distcode) {
    for (;;) {
        int sym = gz_decode(s, lencode);
        if (sym < 0) { gz_fail(s, "invalid literal/length code"); return; }
        if (sym < 256) {
            if (gz_put(s, (unsigned char)sym)) return;
            continue;
        }
        if (sym == 256) return; /* end of block */
        sym -= 257;
        if (sym >= 29) { gz_fail(s, "invalid length code"); return; }
        size_t len = (size_t)GZ_LEN_BASE[sym] + (size_t)gz_bits(s, GZ_LEN_EXTRA[sym]);
        if (s->failed) return;
        int dsym = gz_decode(s, distcode);
        if (dsym < 0 || dsym >= 30) { gz_fail(s, "invalid distance code"); return; }
        size_t dist = (size_t)GZ_DIST_BASE[dsym] + (size_t)gz_bits(s, GZ_DIST_EXTRA[dsym]);
        if (s->failed) return;
        if (dist == 0 || dist > s->outlen) { gz_fail(s, "back-reference before start of output"); return; }
        for (size_t i = 0; i < len; i++) /* byte-wise: overlapping copies are legal */
            if (gz_put(s, s->out[s->outlen - dist])) return;
    }
}

static void gz_fixed(GzState *s) {
    short lengths[288];
    int sym;
    for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < 288; sym++) lengths[sym] = 8;
    GzHuff lencode;
    if (gz_build(&lencode, lengths, 288) < 0) { gz_fail(s, "bad fixed literal table"); return; }
    for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
    GzHuff distcode;
    if (gz_build(&distcode, lengths, 30) < 0) { gz_fail(s, "bad fixed distance table"); return; }
    gz_codes(s, &lencode, &distcode);
}

static void gz_dynamic(GzState *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = gz_bits(s, 5) + 257;
    int ndist = gz_bits(s, 5) + 1;
    int ncode = gz_bits(s, 4) + 4;
    if (s->failed) return;
    if (nlen > 286 || ndist > 30) { gz_fail(s, "too many length or distance symbols"); return; }

    short lengths[286 + 30];
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < ncode; i++) lengths[order[i]] = (short)gz_bits(s, 3);
    if (s->failed) return;

    GzHuff lencode;
    if (gz_build(&lencode, lengths, 19) < 0) { gz_fail(s, "invalid code-length set"); return; }

    int index = 0;
    while (index < nlen + ndist) {
        int sym = gz_decode(s, &lencode);
        if (sym < 0) { gz_fail(s, "invalid code-length code"); return; }
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0) { gz_fail(s, "repeat with no previous length"); return; }
                len = lengths[index - 1];
                rep = 3 + gz_bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + gz_bits(s, 3);
            } else {
                rep = 11 + gz_bits(s, 7);
            }
            if (s->failed) return;
            if (index + rep > nlen + ndist) { gz_fail(s, "code-length repeat overflows"); return; }
            while (rep--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) { gz_fail(s, "missing end-of-block code"); return; }

    if (gz_build(&lencode, lengths, nlen) < 0) { gz_fail(s, "invalid literal/length set"); return; }
    GzHuff distcode;
    if (gz_build(&distcode, lengths + nlen, ndist) < 0) { gz_fail(s, "invalid distance set"); return; }
    gz_codes(s, &lencode, &distcode);
}

int gzip_unpack(const void *data, size_t len, unsigned char **out, size_t *outlen,
                char *err, size_t errlen) {
    if (err && errlen) err[0] = '\0';
    if (!data || !out || !outlen) return -1;
    if (len < 18) { if (err && errlen) snprintf(err, errlen, "gzip member too short"); return -1; }
    const unsigned char *in = (const unsigned char *)data;
    if (in[0] != 0x1f || in[1] != 0x8b) { if (err && errlen) snprintf(err, errlen, "not a gzip member"); return -1; }
    if (in[2] != 8) { if (err && errlen) snprintf(err, errlen, "unsupported gzip compression method"); return -1; }
    unsigned flg = in[3];
    if (flg & 0xe0) { if (err && errlen) snprintf(err, errlen, "reserved gzip flags set"); return -1; }

    size_t pos = 10;
    if (flg & 0x04) { /* FEXTRA */
        if (pos + 2 > len) { if (err && errlen) snprintf(err, errlen, "truncated gzip FEXTRA"); return -1; }
        size_t xlen = (size_t)in[pos] | ((size_t)in[pos + 1] << 8);
        pos += 2 + xlen;
        if (pos > len) { if (err && errlen) snprintf(err, errlen, "truncated gzip FEXTRA"); return -1; }
    }
    if (flg & 0x08) { /* FNAME */
        while (pos < len && in[pos]) pos++;
        if (pos >= len) { if (err && errlen) snprintf(err, errlen, "unterminated gzip FNAME"); return -1; }
        pos++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (pos < len && in[pos]) pos++;
        if (pos >= len) { if (err && errlen) snprintf(err, errlen, "unterminated gzip FCOMMENT"); return -1; }
        pos++;
    }
    if (flg & 0x02) { /* FHCRC */
        pos += 2;
        if (pos > len) { if (err && errlen) snprintf(err, errlen, "truncated gzip FHCRC"); return -1; }
    }

    GzState s;
    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = len;
    s.pos = pos;
    s.err = err;
    s.errlen = errlen;

    int last = 0;
    do {
        last = gz_bits(&s, 1);
        int type = gz_bits(&s, 2);
        if (s.failed) break;
        if (type == 0) {
            /* Stored: skip to the byte boundary, then LEN/NLEN (LE) + raw bytes. */
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.pos + 4 > s.inlen) { gz_fail(&s, "truncated stored block header"); break; }
            unsigned l = (unsigned)s.in[s.pos] | ((unsigned)s.in[s.pos + 1] << 8);
            unsigned nl = (unsigned)s.in[s.pos + 2] | ((unsigned)s.in[s.pos + 3] << 8);
            s.pos += 4;
            if ((l ^ 0xffffu) != nl) { gz_fail(&s, "stored block length check failed"); break; }
            if (s.pos + l > s.inlen) { gz_fail(&s, "truncated stored block"); break; }
            for (unsigned i = 0; i < l; i++)
                if (gz_put(&s, s.in[s.pos + i])) break;
            s.pos += l;
            if (s.failed) break;
        } else if (type == 1) {
            gz_fixed(&s);
        } else if (type == 2) {
            gz_dynamic(&s);
        } else {
            gz_fail(&s, "invalid deflate block type");
        }
        if (s.failed) break;
    } while (!last);

    if (!s.failed) {
        /* RFC 1951 §3.2.1: the final block is padded with ignored bits up to
         * the next byte boundary, and the bit reader pre-loads whole bytes, so
         * the trailer starts at ceil(consumed_bits / 8) - computed from what
         * the reader actually consumed, never from s.pos directly. */
        size_t consumed = 8 * s.pos - (size_t)s.bitcnt;
        size_t tpos = (consumed + 7) / 8;
        if (tpos + 8 > len) {
            gz_fail(&s, "truncated gzip trailer");
        } else {
            uint32_t crc = (uint32_t)in[tpos] | ((uint32_t)in[tpos + 1] << 8) |
                           ((uint32_t)in[tpos + 2] << 16) | ((uint32_t)in[tpos + 3] << 24);
            uint32_t isize = (uint32_t)in[tpos + 4] | ((uint32_t)in[tpos + 5] << 8) |
                             ((uint32_t)in[tpos + 6] << 16) | ((uint32_t)in[tpos + 7] << 24);
            if (crc != gzip_crc32(s.out, s.outlen)) gz_fail(&s, "gzip CRC32 mismatch");
            else if (isize != (uint32_t)(s.outlen & 0xffffffffu)) gz_fail(&s, "gzip ISIZE mismatch");
        }
    }

    if (s.failed) { free(s.out); return -1; }
    if (!s.out) { s.out = (unsigned char *)malloc(1); if (!s.out) return -1; }
    *out = s.out;
    *outlen = s.outlen;
    return 0;
}
