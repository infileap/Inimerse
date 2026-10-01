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

    puts("crp session probe: ok"); return 0;
}
