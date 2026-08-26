/* Cross-platform child process registry. */
#include "child_proc.h"
#include "platform/platform.h"
#include "platform/sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ChildProcEntry g_table[CHILD_PROC_MAX]; static ImMutex *g_lock; static int g_inited;
void child_proc_init(void){if(g_inited)return;g_lock=im_mutex_new();memset(g_table,0,sizeof g_table);g_inited=g_lock!=NULL;}
void child_proc_shutdown(void){if(!g_inited)return;child_proc_kill_tag("");im_mutex_free(g_lock);g_lock=NULL;g_inited=0;}
static void lock_table(void){if(g_lock)im_mutex_lock(g_lock);} static void unlock_table(void){if(g_lock)im_mutex_unlock(g_lock);}
DWORD child_proc_spawn(const char *cmdline,const char *tag,int new_console){if(!g_inited)child_proc_init();if(!g_inited||!cmdline)return 0;child_proc_prune();ImProcess *p=im_process_spawn(cmdline,new_console);if(!p)return 0;lock_table();int slot=-1;for(int i=0;i<CHILD_PROC_MAX;i++)if(!g_table[i].in_use){slot=i;break;}if(slot<0)slot=0;if(g_table[slot].process)im_process_close(g_table[slot].process);g_table[slot].process=p;g_table[slot].pid=(DWORD)im_process_pid(p);snprintf(g_table[slot].cmd,sizeof g_table[slot].cmd,"%s",cmdline);snprintf(g_table[slot].tag,sizeof g_table[slot].tag,"%s",tag?tag:"");g_table[slot].start_ms=im_platform_now_ms();g_table[slot].in_use=1;DWORD pid=g_table[slot].pid;unlock_table();return pid;}
int child_proc_is_alive(DWORD pid){if(!pid)return 0;lock_table();int alive=0;for(int i=0;i<CHILD_PROC_MAX;i++)if(g_table[i].in_use&&g_table[i].pid==pid){alive=im_process_alive(g_table[i].process);break;}unlock_table();return alive;}
int child_proc_kill(DWORD pid){if(!pid)return 0;lock_table();int killed=0;for(int i=0;i<CHILD_PROC_MAX;i++)if(g_table[i].in_use&&g_table[i].pid==pid){killed=im_process_kill(g_table[i].process)==0;im_process_close(g_table[i].process);memset(&g_table[i],0,sizeof g_table[i]);break;}unlock_table();return killed;}
int child_proc_kill_tag(const char *prefix){if(!prefix)prefix="";lock_table();int killed=0;for(int i=0;i<CHILD_PROC_MAX;i++)if(g_table[i].in_use&&(!*prefix||strncmp(g_table[i].tag,prefix,strlen(prefix))==0)){if(im_process_kill(g_table[i].process)==0)++killed;im_process_close(g_table[i].process);memset(&g_table[i],0,sizeof g_table[i]);}unlock_table();return killed;}
char *child_proc_list(void){lock_table();char *out=(char*)calloc(1,16384);if(!out){unlock_table();return NULL;}int pos=0;for(int i=0;i<CHILD_PROC_MAX;i++)if(g_table[i].in_use&&im_process_alive(g_table[i].process)){uint64_t up=(im_platform_now_ms()-g_table[i].start_ms)/1000;int n=snprintf(out+pos,16384-pos,"%llu|%s|%s|%llu\n",(unsigned long long)g_table[i].pid,g_table[i].cmd,g_table[i].tag,(unsigned long long)up);if(n<0||pos+n>=16383)break;pos+=n;}unlock_table();return out;}
void child_proc_prune(void){lock_table();for(int i=0;i<CHILD_PROC_MAX;i++)if(g_table[i].in_use&&!im_process_alive(g_table[i].process)){im_process_close(g_table[i].process);memset(&g_table[i],0,sizeof g_table[i]);}unlock_table();}
