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
    puts("crp session probe: ok"); return 0;
}
