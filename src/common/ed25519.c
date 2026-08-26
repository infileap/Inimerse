/* ed25519.c - compact RFC8032 Ed25519 with internal SHA-512.
 * Field arithmetic: 4x64-bit limbs, __int128 intermediates, mod p = 2^255-19.
 * Points: extended twisted Edwards coordinates. */
#include "ed25519.h"
#include <string.h>
#include <stdint.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef unsigned __int128 u128;
typedef u64 fe[4];

static const u64 P[4] = {0xffffffffffffffedULL, 0xffffffffffffffffULL,
                         0xffffffffffffffffULL, 0x7fffffffffffffffULL};
static const u64 L[4] = {0x5812631a5cf5d3edULL, 0x14def9dea2f79cd6ULL,
                         0x0000000000000000ULL, 0x1000000000000000ULL};
static const u64 D[4] = {0x75eb4dca135978a3ULL, 0x00700a4d4141d8abULL,
                         0x8cc740797779e898ULL, 0x52036cee2b6ffe73ULL}; /* d = -121665/121666 */
static const u64 SQRTM1[4] = {0xc4ee1b274a0ea0b0ULL, 0x2f431806ad2fe478ULL,
                              0x2b4d00993dfbd7a7ULL, 0x2b8324804fc1df0bULL}; /* sqrt(-1) */
static const u64 BASE_X[4] = {0xc9562d608f25d51aULL, 0x692cc7609525a7b2ULL,
                              0xc0a4e231fdd6dc5cULL, 0x216936d3cd6e53feULL};
static const u64 BASE_Y[4] = {0x6666666666666658ULL, 0x6666666666666666ULL,
                              0x6666666666666666ULL, 0x6666666666666666ULL};

/* ---------- field ops ---------- */
static void fe_add(fe r, const fe a, const fe b) {
    u128 c = 0;
    for (int i = 0; i < 4; i++) { c += (u128)a[i] + b[i]; r[i] = (u64)c; c >>= 64; }
    int ge = 1;
    for (int i = 3; i >= 0; i--) { if (r[i] != P[i]) { ge = r[i] > P[i]; break; } }
    if (ge) {
        u64 borrow = 0;
        for (int i = 0; i < 4; i++) {
            u128 cc2 = (u128)r[i] - P[i] - borrow;
            r[i] = (u64)cc2;
            borrow = (u64)(cc2 >> 64) & 1;
        }
    }
}
static void fe_sub(fe r, const fe a, const fe b) {
    u64 t[4];
    u64 borrow = 0;
    for (int i = 0; i < 4; i++) {
        u128 c = (u128)a[i] - b[i] - borrow;
        t[i] = (u64)c;
        borrow = (u64)(c >> 64) & 1;
    }
    if (borrow) {
        u128 cc = 0;
        for (int i = 0; i < 4; i++) { cc += (u128)t[i] + P[i]; t[i] = (u64)cc; cc >>= 64; }
    }
    memcpy(r, t, sizeof t);
}
static void fe_neg(fe r, const fe a) {
    fe z; memset(z, 0, sizeof z);
    fe_sub(r, z, a);
}
static void fe_mul(fe r, const fe a, const fe b) {
    u64 a0[8], b0[8]; /* 32-bit limbs */
    for (int i = 0; i < 4; i++) {
        a0[i*2] = a[i] & 0xffffffff; a0[i*2+1] = a[i] >> 32;
        b0[i*2] = b[i] & 0xffffffff; b0[i*2+1] = b[i] >> 32;
    }
    u64 t[16] = {0};
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            u64 prod = (u64)a0[i] * b0[j];
            u64 lo = prod & 0xffffffff, hi = prod >> 32;
            u64 sum = t[i+j] + lo;
            t[i+j] = sum & 0xffffffff;
            u64 c1 = sum >> 32;
            sum = t[i+j+1] + hi + c1;
            t[i+j+1] = sum & 0xffffffff;
            u64 c2 = sum >> 32;
            for (int k = i+j+2; c2 && k < 16; k++) { sum = t[k] + c2; t[k] = sum & 0xffffffff; c2 = sum >> 32; }
        }
    u64 big[8];
    for (int i = 0; i < 8; i++) big[i] = ((u64)t[i*2+1] << 32) | t[i*2];
    /* reduce mod p: 2^256 = 38 (mod p); then fold bits >= 2^255 (2^255 = 19) */
    u128 acc[4];
    for (int i = 0; i < 4; i++) acc[i] = (u128)big[i] + (u128)big[i+4] * 38;
    u128 c = 0;
    for (int i = 0; i < 4; i++) { c += acc[i]; r[i] = (u64)c; c >>= 64; }
    u128 t0 = (u128)r[0] + c * 38; r[0] = (u64)t0;
    u64 cc = (u64)(t0 >> 64);
    if (cc) {
        u128 t1 = (u128)r[1] + cc; r[1] = (u64)t1; cc = (u64)(t1 >> 64);
        if (cc) { u128 t2 = (u128)r[2] + cc; r[2] = (u64)t2; cc = (u64)(t2 >> 64);
            if (cc) { u128 t3 = (u128)r[3] + cc; r[3] = (u64)t3; } }
    }
    for (int iter = 0; iter < 16; iter++) {
        u64 hi = r[3] >> 63;
        if (!hi) break;
        r[3] &= 0x7fffffffffffffffULL;
        u128 t = (u128)r[0] + (u128)hi * 19; r[0] = (u64)t;
        u64 c2 = (u64)(t >> 64);
        if (c2) { u128 t1 = (u128)r[1] + c2; r[1] = (u64)t1; c2 = (u64)(t1 >> 64);
            if (c2) { u128 t2 = (u128)r[2] + c2; r[2] = (u64)t2; c2 = (u64)(t2 >> 64);
                if (c2) { u128 t3 = (u128)r[3] + c2; r[3] = (u64)t3; } } }
    }
    int ge = 1;
    for (int i = 3; i >= 0; i--) { if (r[i] != P[i]) { ge = r[i] > P[i]; break; } }
    if (ge) {
        u64 borrow = 0;
        for (int i = 0; i < 4; i++) {
            u128 cc2 = (u128)r[i] - P[i] - borrow;
            r[i] = (u64)cc2;
            borrow = (u64)(cc2 >> 64) & 1;
        }
    }
}
static void fe_sq(fe r, const fe a) { fe_mul(r, a, a); }
static void fe_inv(fe r, const fe a) {
    /* a^(p-2), p-2 = 2^255 - 21 (MSB-first square-and-multiply) */
    unsigned char e[32];
    e[0] = 0xEB;
    for (int i = 1; i < 31; i++) e[i] = 0xFF;
    e[31] = 0x7F;
    fe acc = {1,0,0,0};
    for (int b = 254; b >= 0; b--) {
        fe_sq(acc, acc);
        if ((e[b >> 3] >> (b & 7)) & 1) fe_mul(acc, acc, a);
    }
    memcpy(r, acc, sizeof acc);
}

/* ---------- SHA-512 ---------- */
static const u64 K512[80] = {
0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL};
static u64 rotr64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }
static void sha512_block(u64 h[8], const unsigned char *in) {
    u64 w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((u64)in[i*8] << 56) | ((u64)in[i*8+1] << 48) | ((u64)in[i*8+2] << 40) |
               ((u64)in[i*8+3] << 32) | ((u64)in[i*8+4] << 24) | ((u64)in[i*8+5] << 16) |
               ((u64)in[i*8+6] << 8) | (u64)in[i*8+7];
    for (int i = 16; i < 80; i++) {
        u64 s0 = rotr64(w[i-15],1) ^ rotr64(w[i-15],8) ^ (w[i-15] >> 7);
        u64 s1 = rotr64(w[i-2],19) ^ rotr64(w[i-2],61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u64 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 80; i++) {
        u64 S1 = rotr64(e,14) ^ rotr64(e,18) ^ rotr64(e,41);
        u64 ch = (e & f) ^ ((~e) & g);
        u64 t1 = hh + S1 + ch + K512[i] + w[i];
        u64 S0 = rotr64(a,28) ^ rotr64(a,34) ^ rotr64(a,39);
        u64 maj = (a & b) ^ (a & c) ^ (b & c);
        u64 t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}
void sha512_buf(const unsigned char *data, size_t len, unsigned char out[64]) {
    u64 h[8] = {0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,
                0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,
                0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL};
    u64 total = (u64)len;
    while (len >= 128) { sha512_block(h, data); data += 128; len -= 128; }
    unsigned char buf[256];
    size_t rem = len;
    memcpy(buf, data, rem);
    buf[rem] = 0x80;
    size_t padlen = (rem + 17 <= 128) ? 128 - rem - 1 - 8 : 256 - rem - 1 - 8;
    memset(buf + rem + 1, 0, padlen);
    u64 bits = total * 8;
    size_t total_rem = rem + 1 + padlen + 8;
    int bits_at = (total_rem > 128) ? 256 - 8 : 128 - 8;
    for (int i = 0; i < 8; i++) buf[bits_at + i] = (unsigned char)(bits >> (56 - 8*i));
    sha512_block(h, buf);
    if (total_rem > 128) sha512_block(h, buf + 128);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            out[i*8+j] = (unsigned char)(h[i] >> (56 - 8*j));
}

/* ---------- scalar helpers ---------- */
static int u64_ge(const u64 a[4], const u64 b[4]) {
    for (int i = 3; i >= 0; i--) { if (a[i] != b[i]) return a[i] > b[i]; }
    return 1;
}
static void sub_L(u64 s[4]) {
    u64 borrow = 0;
    for (int i = 0; i < 4; i++) {
        u128 cc = (u128)s[i] - L[i] - borrow;
        s[i] = (u64)cc;
        borrow = (u64)(cc >> 64) & 1;
    }
}
static void scalar_reduce(u64 s[4]) {
    while (u64_ge(s, L)) sub_L(s);
}
/* 512-bit (8 limbs) -> 256-bit mod L */
static void scalar_mod_L(u64 r[8]) {
    /* long division: while r >= L<<shift, subtract L<<shift (9-limb, overflow-safe) */
    for (int shift = 260; shift >= 0; shift--) {
        u64 lc[9] = {0};
        int words = shift / 64, bits = shift % 64;
        for (int j = 0; j < 4; j++) {
            u128 sh = (u128)L[j] << bits;
            u64 hi = bits ? (j > 0 ? L[j-1] >> (64 - bits) : 0) : 0;
            lc[words + j] |= ((u64)sh) | hi;
            lc[words + j + 1] += (u64)(sh >> 64);
        }
        u128 carry = 0;
        for (int j = 0; j < 9; j++) { carry += lc[j]; lc[j] = (u64)carry; carry >>= 64; }
        for (;;) {
            int ge = 1;
            for (int j = 8; j >= 0; j--) { u64 rj = j < 8 ? r[j] : 0; if (rj != lc[j]) { ge = rj > lc[j]; break; } }
            if (!ge) break;
            u64 borrow = 0;
            for (int j = 0; j < 8; j++) {
                u128 cc = (u128)r[j] - lc[j] - borrow;
                r[j] = (u64)cc;
                borrow = (u64)(cc >> 64) & 1;
            }
        }
    }
    for (int j = 4; j < 8; j++) r[j] = 0;
    while (u64_ge(r, L)) sub_L(r);
}

/* ---------- point ops ---------- */
typedef struct { fe X, Y, Z, T; } ep;
static void ep_set_base(ep *p) {
    memcpy(p->X, BASE_X, sizeof BASE_X);
    memcpy(p->Y, BASE_Y, sizeof BASE_Y);
    memset(p->Z, 0, sizeof p->Z); p->Z[0] = 1;
    fe_mul(p->T, p->X, p->Y);
}
static void ep_set_identity(ep *p) {
    memset(p->X, 0, sizeof p->X);
    memset(p->Y, 0, sizeof p->Y); p->Y[0] = 1;
    memset(p->Z, 0, sizeof p->Z); p->Z[0] = 1;
    memset(p->T, 0, sizeof p->T);
}
static void ep_double(ep *r, const ep *p) {
    fe A,B,C,D,E,F,G,H,t;
    fe_sq(A, p->X);
    fe_sq(B, p->Y);
    fe_sq(C, p->Z); fe_add(C, C, C);
    fe_neg(D, A);
    fe_add(t, p->X, p->Y); fe_sq(t, t); fe_sub(t, t, A); fe_sub(t, t, B); memcpy(E, t, sizeof t);
    fe_add(G, D, B);
    fe_sub(F, G, C);
    fe_sub(H, D, B);
    fe_mul(r->X, E, F);
    fe_mul(r->Y, G, H);
    fe_mul(r->T, E, H);
    fe_mul(r->Z, F, G);
}
static void ep_add(ep *r, const ep *p, const ep *q) {
    fe A,B,C,CC,DD,E,F,G,H,t;
    fe_sub(t, p->Y, p->X); fe_sub(A, q->Y, q->X); fe_mul(A, t, A);
    fe_add(t, p->Y, p->X); fe_add(B, q->Y, q->X); fe_mul(B, t, B);
    fe_mul(CC, p->T, q->T); fe_mul(CC, CC, D); fe_add(CC, CC, CC);
    fe_mul(DD, p->Z, q->Z); fe_add(DD, DD, DD);
    fe_sub(E, B, A);
    fe_sub(F, DD, CC);
    fe_add(G, DD, CC);
    fe_add(H, B, A);
    fe_mul(r->X, E, F);
    fe_mul(r->Y, G, H);
    fe_mul(r->T, E, H);
    fe_mul(r->Z, F, G);
}
static void ep_scalarmul(ep *r, const unsigned char s[32], const ep *base) {
    ep acc; ep_set_identity(&acc);
    ep b; memcpy(&b, base, sizeof b);
    for (int bit = 255; bit >= 0; bit--) {
        ep_double(&acc, &acc);
        if ((s[bit >> 3] >> (bit & 7)) & 1) ep_add(&acc, &acc, &b);
    }
    memcpy(r, &acc, sizeof acc);
}
static void ep_encode(unsigned char enc[32], const ep *p) {
    fe zi; fe_inv(zi, p->Z);
    fe x, y;
    fe_mul(x, p->X, zi);
    fe_mul(y, p->Y, zi);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            enc[i*8+j] = (unsigned char)(y[i] >> (8*j));
    enc[31] |= (unsigned char)((x[0] & 1) << 7);
}
static int ep_decode(ep *p, const unsigned char enc[32]) {
    fe y, x, xx, u, v, x1, x2, t;
    for (int i = 0; i < 4; i++) {
        y[i] = 0;
        for (int j = 0; j < 8; j++) y[i] |= (u64)enc[i*8+j] << (8*j);
    }
    int sign = (int)(enc[31] >> 7);
    y[3] &= 0x7fffffffffffffffULL;
    fe_sq(u, y);
    fe_sub(u, u, (fe){1,0,0,0});           /* y^2 - 1 */
    fe_sq(v, y); fe_mul(v, v, D); fe_add(v, v, (fe){1,0,0,0}); /* d y^2 + 1 */
    fe_inv(v, v);
    fe_mul(xx, u, v);                       /* x^2 */
    /* x = xx^((p+3)/8); (p+3)/8 = 2^252 - 2 (MSB-first) */
    {
        unsigned char e[32];
        e[0] = 0xFE;
        for (int i = 1; i < 31; i++) e[i] = 0xFF;
        e[31] = 0x0F;
        fe acc = {1,0,0,0};
        for (int b = 254; b >= 0; b--) {
            fe_sq(acc, acc);
            if ((e[b >> 3] >> (b & 7)) & 1) fe_mul(acc, acc, xx);
        }
        memcpy(x1, acc, sizeof acc);
    }
    fe_sq(x2, x1);
    int ok = 1;
    for (int i = 0; i < 4; i++) if (x2[i] != xx[i]) ok = 0;
    if (!ok) {
        fe_mul(x1, x1, SQRTM1);
        fe_sq(x2, x1);
        ok = 1;
        for (int i = 0; i < 4; i++) if (x2[i] != xx[i]) ok = 0;
        if (!ok) return 0;
    }
    if ((x1[0] & 1) != (unsigned int)sign) fe_neg(x1, x1);
    memcpy(p->X, x1, sizeof x1);
    memcpy(p->Y, y, sizeof y);
    memset(p->Z, 0, sizeof p->Z); p->Z[0] = 1;
    fe_mul(p->T, x1, y);
    return 1;
}
static void clamp_scalar(unsigned char h[32]) {
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;
}

void ed25519_pubkey(const unsigned char seed[32], unsigned char pub[32]) {
    unsigned char h[64];
    sha512_buf(seed, 32, h);
    clamp_scalar(h);
    ep base; ep_set_base(&base);
    ep q; ep_scalarmul(&q, h, &base);
    ep_encode(pub, &q);
}
void ed25519_sign(const unsigned char seed[32], const unsigned char *msg, size_t msglen, unsigned char sig[64]) {
    unsigned char h[64];
    sha512_buf(seed, 32, h);
    clamp_scalar(h);
    unsigned char a[32]; memcpy(a, h, 32);
    ep base; ep_set_base(&base);
    ep q; ep_scalarmul(&q, a, &base);
    unsigned char pub[32]; ep_encode(pub, &q);
    /* r = SHA512(prefix || msg) mod L */
    u64 rbuf[8]; unsigned char rb[64];
    {
        unsigned char inp[128 + 8192];
        memcpy(inp, h + 32, 32);
        size_t off = 32;
        for (size_t i = 0; i < msglen && off < sizeof inp; i++) inp[off++] = msg[i];
        sha512_buf(inp, off, rb);
    }
    memcpy(rbuf, rb, 64);
    scalar_mod_L(rbuf);
    unsigned char r32[32]; memcpy(r32, rbuf, 32);
    ep R; ep_scalarmul(&R, r32, &base);
    unsigned char rEnc[32]; ep_encode(rEnc, &R);
    /* k = SHA512(R || A || M) mod L */
    u64 kk[8]; unsigned char kb[64];
    {
        unsigned char inp[128 + 8192];
        size_t off = 0;
        for (size_t i = 0; i < 32 && off < sizeof inp; i++) inp[off++] = rEnc[i];
        for (size_t i = 0; i < 32 && off < sizeof inp; i++) inp[off++] = pub[i];
        for (size_t i = 0; i < msglen && off < sizeof inp; i++) inp[off++] = msg[i];
        sha512_buf(inp, off, kb);
    }
    memcpy(kk, kb, 64);
    scalar_mod_L(kk);
    /* S = (r + k*a) mod L (column carry multiply, overflow-safe) */
    {
        u64 t[8] = {0};
        for (int i = 0; i < 4; i++) {
            u128 carry = 0;
            for (int j = 0; j < 4; j++) {
                u128 cur = (u128)t[i+j] + (u128)((u64*)kk)[i] * ((u64*)a)[j] + carry;
                t[i+j] = (u64)cur;
                carry = cur >> 64;
            }
            u128 c2 = (u128)t[i+4] + carry;
            t[i+4] = (u64)c2;
            u64 extra = (u64)(c2 >> 64);
            for (int k = i+5; extra && k < 8; k++) {
                u128 ck = (u128)t[k] + extra;
                t[k] = (u64)ck;
                extra = (u64)(ck >> 64);
            }
        }
        u64 s[8]; memcpy(s, t, sizeof t);
        u128 c = 0;
        for (int i = 0; i < 4; i++) { c += (u128)s[i] + rbuf[i]; s[i] = (u64)c; c >>= 64; }
        scalar_mod_L(s);
        memcpy(sig + 32, s, 32);
    }
    memcpy(sig, rEnc, 32);
}
int ed25519_verify(const unsigned char pub[32], const unsigned char *msg, size_t msglen, const unsigned char sig[64]) {
    ep A; if (!ep_decode(&A, pub)) return 0;
    ep base; ep_set_base(&base);
    u64 kk[8]; unsigned char kb[64];
    {
        unsigned char inp[128 + 8192];
        size_t off = 0;
        for (size_t i = 0; i < 32 && off < sizeof inp; i++) inp[off++] = sig[i];
        for (size_t i = 0; i < 32 && off < sizeof inp; i++) inp[off++] = pub[i];
        for (size_t i = 0; i < msglen && off < sizeof inp; i++) inp[off++] = msg[i];
        sha512_buf(inp, off, kb);
    }
    memcpy(kk, kb, 64);
    scalar_mod_L(kk);
    ep sB, kA;
    ep_scalarmul(&sB, sig + 32, &base);
    ep_scalarmul(&kA, (unsigned char*)kk, &A);
    fe_neg(kA.X, kA.X);
    fe_neg(kA.T, kA.T);
    ep R; ep_add(&R, &sB, &kA);
    unsigned char rEnc[32]; ep_encode(rEnc, &R);
    return memcmp(rEnc, sig, 32) == 0;
}
