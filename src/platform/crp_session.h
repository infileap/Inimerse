#ifndef INIMERSE_CRP_SESSION_H
#define INIMERSE_CRP_SESSION_H
#include <stdint.h>
typedef enum { IM_CRP_IDLE, IM_CRP_RUNNING, IM_CRP_STOPPED, IM_CRP_CRASHED, IM_CRP_INCOMPATIBLE } ImCrpState;
typedef struct { ImCrpState state; uint64_t heartbeat; uint64_t last_timestamp; int abi; char error[128]; } ImCrpSession;
void im_crp_session_init(ImCrpSession *s, int abi);
int im_crp_session_apply(ImCrpSession *s, const char *type, uint64_t seq, uint64_t timestamp, const char *error);
int im_crp_session_auth(const char *provided, const char *expected);
int im_crp_session_reset(ImCrpSession *s);
#endif
