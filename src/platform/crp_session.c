#include "crp_session.h"
#include <string.h>
#include <stdio.h>
void im_crp_session_init(ImCrpSession *s, int abi) { if (!s) return; memset(s, 0, sizeof *s); s->state = IM_CRP_IDLE; s->abi = abi; }
int im_crp_session_apply(ImCrpSession *s, const char *type, uint64_t seq, uint64_t timestamp, const char *error) {
    if (!s || !type) return -1;
    if (!strcmp(type, "heartbeat")) { if (seq < s->heartbeat) return -2; s->heartbeat = seq; s->last_timestamp = timestamp; return 0; }
    if (!strcmp(type, "start")) { if (s->state == IM_CRP_CRASHED || s->state == IM_CRP_INCOMPATIBLE) return -3; s->state = IM_CRP_RUNNING; return 0; }
    if (!strcmp(type, "stop")) { s->state = IM_CRP_STOPPED; return 0; }
    if (!strcmp(type, "crash")) { s->state = IM_CRP_CRASHED; snprintf(s->error, sizeof s->error, "%s", error && *error ? error : "unknown crash"); return 0; }
    if (!strcmp(type, "incompatible")) { s->state = IM_CRP_INCOMPATIBLE; snprintf(s->error, sizeof s->error, "%s", error && *error ? error : "ABI incompatible"); return 0; }
    return 1;
}
int im_crp_session_auth(const char *provided, const char *expected) {
    if (!provided || !expected) return 0; size_t a = strlen(provided), b = strlen(expected), n = a > b ? a : b; unsigned char diff = (unsigned char)(a ^ b);
    for (size_t i = 0; i < n; ++i) diff |= (unsigned char)(i < a ? provided[i] : 0) ^ (unsigned char)(i < b ? expected[i] : 0);
    return diff == 0;
}
int im_crp_session_reset(ImCrpSession *s) { if (!s) return -1; int abi = s->abi; im_crp_session_init(s, abi); return 0; }
