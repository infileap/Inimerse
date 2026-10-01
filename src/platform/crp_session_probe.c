#include "crp_session.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    ImCrpSession s; im_crp_session_init(&s, 1);
    if (s.state != IM_CRP_IDLE || im_crp_session_apply(&s, "start", 0, 0, NULL) != 0 || s.state != IM_CRP_RUNNING) return 2;
    if (im_crp_session_apply(&s, "heartbeat", 4, 100, NULL) != 0 || im_crp_session_apply(&s, "heartbeat", 3, 101, NULL) != -2 || im_crp_session_apply(&s, "heartbeat", 5, 99, NULL) != -4) return 3;
    if (im_crp_session_apply(&s, "crash", 0, 0, "boom") != 0 || s.state != IM_CRP_CRASHED || strcmp(s.error, "boom")) return 4;
    if (im_crp_session_apply(&s, "start", 0, 0, NULL) != -3) return 5;
    if (!im_crp_session_auth("token", "token") || im_crp_session_auth("token", "other")) return 6;
    im_crp_session_reset(&s); if (s.state != IM_CRP_IDLE || s.abi != 1) return 7;

    /* handshake + capability negotiation (§55.3) */
    ImCrpHello h;
    if (im_crp_session_hello(1, IM_CRP_CAP_EVENTS | IM_CRP_CAP_SNAPSHOT, 1,
                             IM_CRP_CAP_EVENTS | IM_CRP_CAP_UDP, &h) != 0) return 8;
    if (!h.compatible || h.negotiated_caps != IM_CRP_CAP_EVENTS) return 9;
    if (im_crp_session_hello(1, 0, 2, 0, &h) != -1 || h.compatible) return 10;
    if (!strstr(h.error, "version mismatch")) return 11;

    /* lease begin/renew/expiry (§55.5) */
    im_crp_session_reset(&s);
    if (im_crp_session_lease_begin(&s, 1000, 500) != 0 || !s.lease_id[0]) return 12;
    if (im_crp_session_lease_expired(&s, 1400)) return 13;
    if (im_crp_session_lease_touch(&s, 1400, 500) != 0) return 14;
    if (!im_crp_session_lease_expired(&s, 2000)) return 15;      /* 1900 expiry */
    if (im_crp_session_lease_touch(&s, 2001, 500) != -1) return 16;

    /* message sequencing: accept/duplicate/gap (§55.6) */
    im_crp_session_reset(&s);
    if (im_crp_session_accept(&s, 1) != 1) return 17;
    if (im_crp_session_accept(&s, 1) != 0) return 18;             /* duplicate */
    if (im_crp_session_accept(&s, 3) != -1) return 19;            /* gap */
    if (im_crp_session_accept(&s, 2) != 1) return 20;
    if (im_crp_session_accept(&s, 3) != 1) return 21;

    /* resume plan: replay within window, snapshot beyond it */
    int rf = 0, snap = 0;
    im_crp_session_resume_plan(&s, 2, 16, &rf, &snap);
    if (rf != 3 || snap != 0) return 22;
    im_crp_session_resume_plan(&s, 0, 1, &rf, &snap);   /* gap 3 > window 1 */
    if (rf != 0 || snap != 1) return 23;

    /* §55.6 lifecycle: a dropped connection is not a departure */
    im_crp_session_reset(&s);
    if (im_crp_session_apply(&s, "start", 0, 0, NULL) != 0) return 24;
    if (im_crp_session_disconnect(&s, 1000, 500) != 0 || s.state != IM_CRP_DISCONNECTED_GRACE) return 25;
    if (im_crp_session_grace_expired(&s, 1400) || !im_crp_session_grace_expired(&s, 1600)) return 26;

    /* reattach within the same generation replays from the last commit */
    im_crp_session_reset(&s);
    im_crp_session_apply(&s, "start", 0, 0, NULL);
    if (im_crp_session_accept(&s, 1) != 1 || im_crp_session_accept(&s, 2) != 1 || im_crp_session_accept(&s, 3) != 1) return 27;
    im_crp_session_set_generation(&s, 1);
    if (im_crp_session_accept(&s, 4) != 1) return 28;
    ImCrpResumePlan plan;
    if (im_crp_session_reattach_plan(&s, 1, 3, 3, 16, &plan) != 0) return 29;
    if (plan.needs_snapshot || plan.replay_from != 4 || plan.last_applied != 4 || plan.authority_changed) return 30;

    /* after an authority change the plan must be a snapshot with last_applied
       reset -- replaying across generations could resurrect stale authority */
    im_crp_session_set_generation(&s, 2);
    if (s.last_applied != 0) return 31;
    if (im_crp_session_reattach_plan(&s, 1, 3, 3, 16, &plan) != 0) return 32;
    if (!plan.needs_snapshot || !plan.authority_changed || plan.last_applied != 0) return 33;
    if (strcmp(plan.reason, "authority_changed")) return 34;
    /* the new generation resumes normally */
    if (im_crp_session_reattach_plan(&s, 2, 0, 0, 16, &plan) != 0) return 35;
    if (plan.needs_snapshot || plan.replay_from != 1) return 36;

    /* falling out of the retained window also forces a snapshot */
    im_crp_session_set_generation(&s, 1);
    for (uint64_t q = 1; q <= 40; q++) if (im_crp_session_accept(&s, q) != 1) return 37;
    if (im_crp_session_reattach_plan(&s, 1, 1, 1, 16, &plan) != 0) return 38;
    if (!plan.needs_snapshot || strcmp(plan.reason, "window_exceeded")) return 39;

    /* unacknowledged input stays pending and never becomes authority */
    im_crp_session_reset(&s);
    if (im_crp_session_note_input(&s, 0) != 1 || im_crp_session_note_input(&s, 1) != 1) return 40;
    if (s.pending_inputs != 1 || s.rejected_inputs != 1) return 41;

    /* idempotency: the same key is applied at most once */
    im_crp_session_reset(&s);
    if (im_crp_session_idem_begin(&s, "pay-1") != 1) return 42;
    if (im_crp_session_idem_end(&s, "pay-1", 1) != 0) return 43;
    if (im_crp_session_idem_begin(&s, "pay-1") != 0) return 44;   /* replay */
    if (im_crp_session_idem_begin(&s, "pay-2") != 1) return 45;

    puts("crp session probe: ok"); return 0;
}
