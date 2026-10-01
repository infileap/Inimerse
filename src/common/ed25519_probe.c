/* ed25519_probe.c - known-answer tests plus a deterministic round-trip fuzz.
 *
 * WHY THIS EXISTS: every signature the engine produces is only checked a
 * handful of times per test suite, so a reduction bug that corrupts ~3% of
 * signatures slips through.  Three separate guards, because two distinct real
 * bugs have already escaped this file's absence:
 *
 *   1. RFC 8032 §7.1 TEST 1/2/3 known answers - lock the whole pipeline.
 *   2. SHA-512 known answers at block/padding boundaries AND past 8 KiB - the
 *      signer used to copy R||A||M into a fixed 8320-byte buffer and silently
 *      stop, so signatures over messages longer than ~8 KiB were not valid
 *      RFC 8032 signatures.  A truncated hash cannot match these digests.
 *   3. Ed25519 known answers for 8256 / 8257 / 20000-byte messages, and a
 *      fuzz sweep of round-trips.  The carry-propagation defect that corrupted
 *      1/32 of all signatures (2^256 dropped out of the limb sum) is only
 *      caught statistically: 600 draws give ~18.75 expected failures against
 *      that bug, i.e. P(miss) = (31/32)^600 ~ 5e-9, while keeping this probe
 *      to a few seconds.
 *
 * It is a pure unit probe: no files, no network, no VM.
 * Expected values were produced with python cryptography 46.0.5 (OpenSSL). */
#include <stdio.h>
#include <string.h>

#include "ed25519.h"

#define FUZZ_ROUNDS 600
#define MAX_MSG 20000

static int failures = 0;

static int from_hex(const char *hex, unsigned char *out, size_t outmax) {
    size_t n = strlen(hex);
    if (n % 2 != 0 || n / 2 > outmax) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (unsigned char)v;
    }
    return (int)(n / 2);
}

static void to_hex(const unsigned char *b, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2*i] = d[b[i] >> 4]; out[2*i+1] = d[b[i] & 15]; }
    out[2*n] = 0;
}

/* deterministic message pattern shared with the generator script */
static void pattern_msg(unsigned char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)((i * 37u + 11u) & 0xffu);
}

/* ---------- 1. RFC 8032 §7.1 known answers ---------- */
static void rfc8032_kat(void) {
    static const struct { const char *seed, *msg, *pub, *sig; } v[] = {
        { "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "",
          "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
          "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
        { "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "72",
          "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
          "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
        { "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", "af82",
          "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
          "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        unsigned char seed[32], msg[8], exp_pub[32], exp_sig[64], pub[32], sig[64];
        char got[129];
        int mlen = from_hex(v[i].msg, msg, sizeof msg);
        if (from_hex(v[i].seed, seed, 32) != 32 || mlen < 0 ||
            from_hex(v[i].pub, exp_pub, 32) != 32 ||
            from_hex(v[i].sig, exp_sig, 64) != 64) {
            printf("  rfc8032[%zu]: bad vector in probe\n", i); ++failures; continue;
        }
        ed25519_pubkey(seed, pub);
        if (memcmp(pub, exp_pub, 32) != 0) {
            printf("  rfc8032[%zu]: WRONG PUBLIC KEY\n", i); ++failures;
        }
        ed25519_sign(seed, msg, (size_t)mlen, sig);
        if (memcmp(sig, exp_sig, 64) != 0) {
            to_hex(sig, 64, got);
            printf("  rfc8032[%zu]: WRONG SIGNATURE\n    got %s\n", i, got); ++failures;
        }
        if (!ed25519_verify(exp_pub, msg, (size_t)mlen, exp_sig)) {
            printf("  rfc8032[%zu]: rejected the official signature\n", i); ++failures;
        }
    }
    printf("ed25519_probe: RFC 8032 §7.1 TEST 1/2/3 known answers checked\n");
}

/* ---------- 2. SHA-512 known answers (padding and >8 KiB) ---------- */
static void sha512_kat(void) {
    static const struct { size_t len; const char *digest; } v[] = {
        { 0,     "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
        { 111,   "4d1db900250c96436052fbca79c13acbf378aad9c35b87d94c3803264df61fd22cbd327c8938d024db372abf4208934ee09367d571d6c670bf74ee07b83e7506" },
        { 112,   "dfb715ca3478a894302ace39c42d1d6646e1044f2247a6274d8b42d155d2fdbe7017195e85cfba96bedc51f84c44638978a540039ff09c64cef6c0c5ccc8f7b6" },
        { 113,   "604b00570a65f49111782330fd36bef680bcc58e13288cbec1b8554e81de17b05f53e1293d33673872d0d48a57aba35f539643a3b8210d2bef35531f2b441451" },
        { 127,   "f93a0e7465b294188e8aa2b1cc2e98bc8d5115d46f51c7a9ec599b9d9f96a80fef6a4f226b648c89bd9eac23b3d64264898b568d915c66666c44cd0319e2ef56" },
        { 128,   "0b4815d35f9d07b1a30de2790e1be2a720234295cd7b4d9e9af51719ff90019f1fe6d4e402a7dcc4177085023dc460ab743dad9b2c1dda42662bda5d3b2e155b" },
        { 129,   "1809db04d02717483e04bc4333a14308bd2d0213ba7bf2c63f11eb1b8a0af8252e67fd104fd466fb95f945539824d8e4183155fa5ced0bee3dad46d9384a0bd5" },
        { 1000,  "bef824658455c75d8ed438db9b1c2c26c705f5f0423c3b42834e0a5aded3123efc6be2da589e55e43f3d1d03e6a83134bb9781333bc63f72a0e2559d1263209d" },
        { 8255,  "7eacd24d78df7327f545e0975a12498bbbde69c1f8a52644fd00f07dc4fc5833ecf710b1471de003468f86448c7edb393b76bf338bf5cb5a18ed00e0705b7d68" },
        { 8256,  "42cbbefc30b6db46faf118bfe425a34f063a2a2fd1a2b8d90f049215eccb2e8ffc398cc465318c407c5b96cdad9f661f7f4b5746e28ac6f977b1b57dfcad2eb1" },
        { 8257,  "d522d4c12b509732d4e6b13bd2049abc136d1f57a6c3269138a1c5d761de0ce9957c6574a493027f2db73c564323fdadb9395fe117ca7d17d1f3eba6cfdc774c" },
        { 9000,  "10e9e7b3a15e0f161d7d362da58dcbbdfcd978661c5623af67072ce5f5bee60a092d6fc0cf72d04cfdb1048c9edf77b88f3f11c1b6923e9b8f1acca85e97b347" },
        { 20000, "e80f5f1ad51f353840369cb372b2b93ce88c6dfdb7e860ddaa1c50443ca2b8b21383b979ead8621251f703288abdf2b2626eed3242d597eda92daaab8817210b" },
    };
    static unsigned char buf[MAX_MSG];
    int bad = 0;
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        unsigned char dig[64], exp[64];
        char got[129];
        pattern_msg(buf, v[i].len);
        sha512_buf(buf, v[i].len, dig);
        from_hex(v[i].digest, exp, 64);
        if (memcmp(dig, exp, 64) != 0) {
            to_hex(dig, 64, got);
            printf("  sha512[%zu]: WRONG DIGEST\n    got %s\n", v[i].len, got);
            ++bad;
        }
        /* the streaming API must agree with the one-shot form at any split */
        {
            Sha512Ctx c; unsigned char s[64];
            sha512_init(&c);
            for (size_t k = 0; k < v[i].len; k += 7) {
                size_t take = (v[i].len - k < 7) ? v[i].len - k : 7;
                sha512_update(&c, buf + k, take);
            }
            sha512_final(&c, s);
            if (memcmp(s, exp, 64) != 0) {
                printf("  sha512[%zu]: streaming (7-byte chunks) disagrees\n", v[i].len);
                ++bad;
            }
        }
    }
    failures += bad;
    printf("ed25519_probe: SHA-512 known answers checked (13 lengths, incl. >8 KiB)\n");
}

/* ---------- 3. Ed25519 known answers for long messages ---------- */
static void long_message_kat(void) {
    /* seed 00 01 02 .. 1f */
    static const struct { size_t len; const char *sig; } v[] = {
        { 0,     "9ca53579530654d5c3df77089ef45eda613e2fedf670e96bedac4639504e5845ef4b95d5793077233dd16817b2532e9c5525872a73a4ad74b759369a9e05c102" },
        { 1,     "0a5b5681eef4ef7b0147db3b68a20708d187debe96dd3706e195dd9e1587378d02068ae3d335a4fc2aeee6474b713c846d502907080caec31f9b8bf9d9fa6305" },
        { 8256,  "d7ab8303ef53367861e3aabdff22e064726422d559b014b40c60956daeecaf672eba61d953e8442512315be18a1cd74ff2a47e33320acf79e6a0e86a96ef8409" },
        { 8257,  "4f02a8f3cca3e8ce3f6c3fd835c5aea0774a16f8fd258073b766f6687b5108230f1f52cbb5058b5e0e9b41c647df44a02ed9d72a8f3780c001b8364d85028a00" },
        { 20000, "71ad89d11a603e217dd09d653b3a906d7c533f9c9451abd0fc1575d2f175a0872c6583506a99d7c82fd4fe73abc1ea4148aa74e399f0cb0f45bd8b4891450f02" },
    };
    static const char *pub_hex =
        "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
    static unsigned char buf[MAX_MSG];
    unsigned char seed[32], exp_sig[64], exp_pub[32], pub[32], sig[64];
    char got[129];
    int bad = 0;

    for (int j = 0; j < 32; j++) seed[j] = (unsigned char)j;
    from_hex(pub_hex, exp_pub, 32);
    ed25519_pubkey(seed, pub);
    if (memcmp(pub, exp_pub, 32) != 0) {
        to_hex(pub, 32, got);
        printf("  longmsg: WRONG PUBLIC KEY for seed 00..1f\n    got %s\n", got);
        ++bad;
    }
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        pattern_msg(buf, v[i].len);
        from_hex(v[i].sig, exp_sig, 64);
        ed25519_sign(seed, buf, v[i].len, sig);
        if (memcmp(sig, exp_sig, 64) != 0) {
            to_hex(sig, 64, got);
            printf("  longmsg[%zu]: WRONG SIGNATURE (message not hashed in full?)\n    got %s\n",
                   v[i].len, got);
            ++bad;
        }
    }
    failures += bad;
    printf("ed25519_probe: Ed25519 known answers checked (len 0/1/8256/8257/20000, not truncated)\n");
}

/* ---------- 4. deterministic round-trip fuzz ---------- */
static void fuzz_roundtrips(void) {
    int ok = 0, bad = 0, first_bad = -1;

    for (int i = 0; i < FUZZ_ROUNDS; ++i) {
        unsigned char seed[32], pub[32], sig[64];
        char msg[64];
        int n;

        /* deterministic, well-spread seeds: no RNG, so a failure is
         * reproducible from the index alone */
        for (int j = 0; j < 32; ++j)
            seed[j] = (unsigned char)(((unsigned)i * 131u + (unsigned)j * 17u + 11u) & 0xffu);

        n = snprintf(msg, sizeof msg, "probe-message-%d", i);
        if (n < 0 || (size_t)n >= sizeof msg) { printf("message overflow\n"); ++failures; return; }

        ed25519_pubkey(seed, pub);
        ed25519_sign(seed, (const unsigned char *)msg, (size_t)n, sig);

        if (ed25519_verify(pub, (const unsigned char *)msg, (size_t)n, sig)) {
            ++ok;
        } else {
            if (first_bad < 0) first_bad = i;
            if (bad < 5) printf("  round-trip FAILED at index %d\n", i);
            ++bad;
        }
    }

    printf("ed25519_probe: %d/%d round-trips verified, %d failed", ok, FUZZ_ROUNDS, bad);
    if (first_bad >= 0) printf(" (first failure at index %d)", first_bad);
    printf("\n");

    if (bad) {
        printf("ed25519_probe: a valid signature was rejected -> scalar/reduction bug\n");
        failures += bad;
    }
}

int main(void) {
    rfc8032_kat();
    sha512_kat();
    long_message_kat();
    fuzz_roundtrips();

    if (failures) {
        printf("ed25519_probe: FAILED (%d problems)\n", failures);
        return 1;
    }
    printf("ed25519_probe: all known answers and round-trips OK\n");
    return 0;
}
