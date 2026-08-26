#ifndef INIMERSE_PLATFORM_PROCESS_H
#define INIMERSE_PLATFORM_PROCESS_H

#include <stdint.h>

typedef struct ImProcess ImProcess;

ImProcess *im_process_spawn(const char *command, int new_console);
uint64_t im_process_pid(const ImProcess *process);
int im_process_alive(ImProcess *process);
int im_process_wait(ImProcess *process, unsigned int timeout_ms);
int im_process_kill(ImProcess *process);
void im_process_close(ImProcess *process);

#endif
