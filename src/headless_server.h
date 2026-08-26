#ifndef HEADLESS_SERVER_H
#define HEADLESS_SERVER_H
int headless_init(int port);
void headless_shutdown(void);
int headless_enabled(void);
void headless_accept(void);
int headless_send_frame(const char *json);
int headless_poll_input(char *buf, int cap);
int headless_last_ci(void);
void headless_start_thread(void);
#endif
