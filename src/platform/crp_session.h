#ifndef INIMERSE_CRP_SESSION_H
#define INIMERSE_CRP_SESSION_H
#include <stdint.h>

typedef enum {
    IM_CRP_IDLE, IM_CRP_RUNNING, IM_CRP_STOPPED, IM_CRP_CRASHED, IM_CRP_INCOMPATIBLE,
    /* §55.6 lifecycle: a dropped network is not a player leaving the world */
    IM_CRP_DEGRADED, IM_CRP_DISCONNECTED_GRACE, IM_CRP_REATTACHING,
    IM_CRP_RESUMED, IM_CRP_READ_ONLY, IM_CRP_EXPIRED
} ImCrpState;

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
    /* --- §55.6 extensions --- */
    uint64_t generation;        /* authority generation this session is bound to */
    uint64_t last_committed;    /* highest sequence the authority committed */
    uint64_t grace_until_ms;    /* disconnected_grace deadline */
    int pending_inputs;         /* unacknowledged client inputs (never authority) */
    int rejected_inputs;
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

/* §55.6 lifecycle transitions.  disconnect() enters the grace window (a
   dropped connection is not a departure); reattach_plan() decides how the
   session resumes -- and after an authority change the answer is a snapshot,
   never a replay. */
int im_crp_session_disconnect(ImCrpSession *s, uint64_t now_ms, uint64_t grace_ms);
int im_crp_session_grace_expired(const ImCrpSession *s, uint64_t now_ms);
/* Mark unacknowledged client input: it stays pending/rejected and can never
   silently overwrite authoritative state.  Returns the new pending count. */
int im_crp_session_note_input(ImCrpSession *s, int rejected);

typedef struct {
    int needs_snapshot;      /* 1 = send a snapshot; replay is not possible */
    int read_only;           /* 1 = observe only (cannot take authority back) */
    int authority_changed;   /* generation differs from the session's binding */
    uint64_t generation;     /* the authority generation to resume against */
    uint64_t replay_from;    /* first sequence to replay (0 with a snapshot) */
    uint64_t last_applied;   /* sequence to report; RESET to 0 on generation change */
    char reason[64];
} ImCrpResumePlan;

/* Plan a reattach against the current authority generation.  last_received /
   last_committed are the client's view (white paper §55.6 reconnect fields);
   window is the retained event window size. */
int im_crp_session_reattach_plan(const ImCrpSession *s, uint64_t generation,
                                 uint64_t last_received, uint64_t last_committed,
                                 int window, ImCrpResumePlan *out);
/* Rebinding to a new authority resets the sequence domain. */
int im_crp_session_set_generation(ImCrpSession *s, uint64_t generation);

/* Idempotency keys for side-effecting requests (§55.6: query before retry):
   begin returns 1 for a new key, 0 when this key was already applied. */
#define IM_CRP_IDEM_SLOTS 64
int im_crp_session_idem_begin(ImCrpSession *s, const char *key);
int im_crp_session_idem_end(ImCrpSession *s, const char *key, int applied);

#endif
