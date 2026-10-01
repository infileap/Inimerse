#ifndef INIMERSE_CRP_SESSION_H
#define INIMERSE_CRP_SESSION_H
#include <stdint.h>

typedef enum { IM_CRP_IDLE, IM_CRP_RUNNING, IM_CRP_STOPPED, IM_CRP_CRASHED, IM_CRP_INCOMPATIBLE } ImCrpState;

typedef struct {
    ImCrpState state;
    uint64_t heartbeat;
    uint64_t last_timestamp;
    int abi;
    char error[128];
    /* --- §55.5/§55.6 extensions (appended; old callers keep working) --- */
    uint32_t caps;              /* negotiated capability bits */
    uint64_t last_applied;      /* highest applied message seq */
    uint64_t lease_expires_ms;  /* 0 = no lease */
    uint64_t lease_ttl_ms;
    char lease_id[33];          /* hex lease id ("" = none) */
} ImCrpSession;

/* capability bits negotiated in the CRP handshake (§55.3) */
#define IM_CRP_CAP_EVENTS   0x0001u  /* reliable event stream */
#define IM_CRP_CAP_SNAPSHOT 0x0002u  /* state snapshots / resume from snapshot */
#define IM_CRP_CAP_UDP      0x0004u  /* udp hub transport */
#define IM_CRP_CAP_RELAY    0x0008u  /* relay / portal routing */

typedef struct {
    int my_version, peer_version;
    uint32_t my_caps, peer_caps, negotiated_caps;
    int compatible;
    char error[128];
} ImCrpHello;

void im_crp_session_init(ImCrpSession *s, int abi);
int im_crp_session_apply(ImCrpSession *s, const char *type, uint64_t seq, uint64_t timestamp, const char *error);
int im_crp_session_auth(const char *provided, const char *expected);
int im_crp_session_reset(ImCrpSession *s);

/* Handshake + capability negotiation (§55.3).  Returns 0 compatible (out
   filled with the intersection of capabilities), -1 incompatible (out->error
   names the mismatch; §24.6: never silently pretend compatibility). */
int im_crp_session_hello(int my_version, uint32_t my_caps,
                         int peer_version, uint32_t peer_caps, ImCrpHello *out);

/* Lease (§55.5): begin/renew/expiry.  ttl_ms 0 uses a 30s default.
   begin returns 0 and fills s->lease_id; touch returns 0 while the lease is
   still valid, -1 when it already expired (caller must re-handshake). */
int im_crp_session_lease_begin(ImCrpSession *s, uint64_t now_ms, uint64_t ttl_ms);
int im_crp_session_lease_touch(ImCrpSession *s, uint64_t now_ms, uint64_t ttl_ms);
int im_crp_session_lease_expired(const ImCrpSession *s, uint64_t now_ms);

/* Message sequencing (§55.6): 1 = accept and apply, 0 = duplicate (already
   applied; safe to drop), -1 = gap (missing messages; resync needed). */
int im_crp_session_accept(ImCrpSession *s, uint64_t seq);

/* Resume plan (§55.6): decide replay vs snapshot based on how far behind the
   client's last ack is.  window = retained event window size.  Fills
   *replay_from (first seq to replay, 0 when a snapshot is required) and
   *needs_snapshot (1 = send snapshot instead of replay). */
int im_crp_session_resume_plan(const ImCrpSession *s, uint64_t last_ack_seq, int window,
                               int *replay_from, int *needs_snapshot);

#endif
