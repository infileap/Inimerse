#include "crp_session.h"
#include "../common/sha256.h"
#include <string.h>
#include <stdio.h>
void im_crp_session_init(ImCrpSession *s, int abi) { if (!s) return; memset(s, 0, sizeof *s); s->state = IM_CRP_IDLE; s->abi = abi; }
int im_crp_session_apply(ImCrpSession *s, const char *type, uint64_t seq, uint64_t timestamp, const char *error) {
    if (!s || !type) return -1;
    if (!strcmp(type, "heartbeat")) { if (seq < s->heartbeat) return -2; if (timestamp && s->last_timestamp && timestamp < s->last_timestamp) return -4; s->heartbeat = seq; s->last_timestamp = timestamp; return 0; }
    if (!strcmp(type, "start")) { if (s->state == IM_CRP_CRASHED || s->state == IM_CRP_INCOMPATIBLE) return -3; s->state = IM_CRP_RUNNING; return 0; }
    if (!strcmp(type, "stop")) { s->state = IM_CRP_STOPPED; return 0; }
    if (!strcmp(type, "crash")) { s->state = IM_CRP_CRASHED; snprintf(s->error, sizeof s->error, "%s", error && *error ? error : "unknown crash"); return 0; }
    if (!strcmp(type, "incompatible")) { s->state = IM_CRP_INCOMPATIBLE; snprintf(s->error, sizeof s->error, "%s", error && *error ? error : "ABI incompatible"); return 0; }
    return 1;
}
int im_crp_session_auth(const char *provided, const char *expected) {
    if (!provided || !expected) return 0;
    size_t a = strlen(provided), b = strlen(expected), n = a > b ? a : b;
    unsigned char diff = (unsigned char)(a ^ b);
    for (size_t i = 0; i < n; ++i) diff |= (unsigned char)(i < a ? provided[i] : 0) ^ (unsigned char)(i < b ? expected[i] : 0);
    return diff == 0;
}
int im_crp_session_reset(ImCrpSession *s) { if (!s) return -1; int abi = s->abi; im_crp_session_init(s, abi); return 0; }

/* ================= handshake, lease and message sequencing (§55) ================= */

static unsigned long long crp_mix64(unsigned long long x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

int im_crp_session_hello(int my_version, uint32_t my_caps,
                         int peer_version, uint32_t peer_caps, ImCrpHello *out) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    out->my_version = my_version;
    out->peer_version = peer_version;
    out->my_caps = my_caps;
    out->peer_caps = peer_caps;
    if (my_version != peer_version) {
        out->compatible = 0;
        snprintf(out->error, sizeof out->error,
                 "protocol version mismatch: peer %d, supported %d", peer_version, my_version);
        return -1;
    }
    out->compatible = 1;
    out->negotiated_caps = my_caps & peer_caps;
    return 0;
}

int im_crp_session_lease_begin(ImCrpSession *s, uint64_t now_ms, uint64_t ttl_ms) {
    if (!s) return -1;
    if (ttl_ms == 0) ttl_ms = 30000;
    s->lease_ttl_ms = ttl_ms;
    s->lease_expires_ms = now_ms + ttl_ms;
    unsigned long long id = crp_mix64(now_ms ^ ((unsigned long long)s->abi << 32) ^ (s->heartbeat + 1));
    snprintf(s->lease_id, sizeof s->lease_id, "%016llx%016llx", id, crp_mix64(id));
    return 0;
}

int im_crp_session_lease_touch(ImCrpSession *s, uint64_t now_ms, uint64_t ttl_ms) {
    if (!s || !s->lease_id[0]) return -1;
    if (now_ms > s->lease_expires_ms) return -1; /* expired: re-handshake required */
    if (ttl_ms > 0) s->lease_ttl_ms = ttl_ms;
    if (s->lease_ttl_ms == 0) s->lease_ttl_ms = 30000;
    s->lease_expires_ms = now_ms + s->lease_ttl_ms;
    return 0;
}

int im_crp_session_lease_expired(const ImCrpSession *s, uint64_t now_ms) {
    if (!s || !s->lease_id[0]) return 1;
    return now_ms > s->lease_expires_ms;
}

int im_crp_session_accept(ImCrpSession *s, uint64_t seq) {
    if (!s) return -1;
    if (seq <= s->last_applied) return 0;          /* duplicate: drop */
    if (s->last_applied != 0 && seq > s->last_applied + 1) return -1; /* gap: resync */
    s->last_applied = seq;
    return 1;
}

int im_crp_session_resume_plan(const ImCrpSession *s, uint64_t last_ack_seq, int window,
                               int *replay_from, int *needs_snapshot) {
    if (!s) return -1;
    if (window < 0) window = 0;
    int behind = (s->last_applied >= last_ack_seq);
    uint64_t gap = behind ? (s->last_applied - last_ack_seq) : 0;
    if (behind && gap <= (uint64_t)window) {
        if (replay_from) *replay_from = (int)(last_ack_seq + 1);
        if (needs_snapshot) *needs_snapshot = 0;
        return 0;
    }
    if (replay_from) *replay_from = 0;
    if (needs_snapshot) *needs_snapshot = 1;
    return 0;
}

/* ================= §55.6 lifecycle, reattach and idempotency ================= */

int im_crp_session_disconnect(ImCrpSession *s, uint64_t now_ms, uint64_t grace_ms) {
    if (!s) return -1;
    if (s->state == IM_CRP_EXPIRED || s->state == IM_CRP_READ_ONLY) return -1;
    if (grace_ms == 0) grace_ms = 30000;
    s->grace_until_ms = now_ms + grace_ms;
    s->state = IM_CRP_DISCONNECTED_GRACE;
    return 0;
}

int im_crp_session_grace_expired(const ImCrpSession *s, uint64_t now_ms) {
    if (!s) return 1;
    if (s->state != IM_CRP_DISCONNECTED_GRACE && s->state != IM_CRP_DEGRADED) return 0;
    return now_ms > s->grace_until_ms;
}

int im_crp_session_note_input(ImCrpSession *s, int rejected) {
    if (!s) return -1;
    if (rejected) s->rejected_inputs++;
    else s->pending_inputs++;
    return s->pending_inputs;
}

int im_crp_session_set_generation(ImCrpSession *s, uint64_t generation) {
    if (!s) return -1;
    s->generation = generation;
    /* a new authority means a new sequence domain: nothing carries over */
    s->last_applied = 0;
    s->last_committed = 0;
    s->pending_inputs = 0;
    s->rejected_inputs = 0;
    return 0;
}

int im_crp_session_reattach_plan(const ImCrpSession *s, uint64_t generation,
                                 uint64_t last_received, uint64_t last_committed,
                                 int window, ImCrpResumePlan *out) {
    if (!s || !out) return -1;
    memset(out, 0, sizeof *out);
    out->generation = generation;
    if (window < 0) window = 0;

    if (generation != s->generation) {
        /* authority moved: the old sequence domain is meaningless, so the
           only safe resume is a snapshot and last_applied restarts at 0 --
           replaying across generations could resurrect stale authority */
        out->needs_snapshot = 1;
        out->authority_changed = 1;
        out->last_applied = 0;
        out->replay_from = 0;
        snprintf(out->reason, sizeof out->reason, "%s", "authority_changed");
        return 0;
    }
    out->authority_changed = 0;
    uint64_t have = s->last_applied;
    uint64_t ack = last_committed;
    if (ack > last_received) ack = last_received;
    if (have > ack && (have - ack) > (uint64_t)window) {
        /* the client fell out of the retained window: snapshot instead */
        out->needs_snapshot = 1;
        out->last_applied = have;
        snprintf(out->reason, sizeof out->reason, "%s", "window_exceeded");
        return 0;
    }
    out->needs_snapshot = 0;
    out->replay_from = ack + 1;
    out->last_applied = have;
    snprintf(out->reason, sizeof out->reason, "%s", "replay");
    return 0;
}

/* idempotency keys: a linear list of recent keys, stored as SHA-256 digests
   (canonical request texts outgrow any small inline buffer, and a truncated
   key would silently defeat replay detection) */
typedef struct { char key[65]; int applied; } ImIdemSlot;
static ImIdemSlot *idem_slots(ImCrpSession *s) {
    /* the slots live in a side table keyed by the session pointer to keep
       ImCrpSession layout stable for existing callers */
    static struct { ImCrpSession *s; ImIdemSlot slots[IM_CRP_IDEM_SLOTS]; int n; int head; } g_idem[16];
    static int n_idem = 0;
    for (int i = 0; i < n_idem; ++i) if (g_idem[i].s == s) return g_idem[i].slots;
    if (n_idem >= 16) return g_idem[0].slots;
    g_idem[n_idem].s = s;
    memset(g_idem[n_idem].slots, 0, sizeof g_idem[n_idem].slots);
    return g_idem[n_idem++].slots;
}

static void idem_digest(const char *key, char out[65]) {
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, key, strlen(key));
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    sha256_hex_of_digest(digest, out);
}

int im_crp_session_idem_begin(ImCrpSession *s, const char *key) {
    if (!s || !key || !key[0]) return 1;   /* no key: caller must treat as always-new */
    char dg[65];
    idem_digest(key, dg);
    ImIdemSlot *slots = idem_slots(s);
    for (int i = 0; i < IM_CRP_IDEM_SLOTS; ++i)
        if (slots[i].applied && strcmp(slots[i].key, dg) == 0) return 0;   /* replay */
    for (int i = 0; i < IM_CRP_IDEM_SLOTS; ++i)
        if (!slots[i].applied) { snprintf(slots[i].key, sizeof slots[i].key, "%s", dg); slots[i].applied = 0; return 1; }
    /* table full: replace the first slot (bounded memory beats unbounded growth) */
    snprintf(slots[0].key, sizeof slots[0].key, "%s", dg);
    slots[0].applied = 0;
    return 1;
}

int im_crp_session_idem_end(ImCrpSession *s, const char *key, int applied) {
    if (!s || !key || !key[0]) return -1;
    char dg[65];
    idem_digest(key, dg);
    ImIdemSlot *slots = idem_slots(s);
    for (int i = 0; i < IM_CRP_IDEM_SLOTS; ++i)
        if (strcmp(slots[i].key, dg) == 0) { slots[i].applied = applied ? 1 : 0; return 0; }
    return -1;
}
