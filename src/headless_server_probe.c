#ifndef _WIN32
#include "headless_server.h"
#include "platform/socket.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    char input[256];
    if (!headless_init(18130)) return 2;
    headless_start_thread();
    ImSocket *client = NULL;
    for (int i = 0; i < 40 && !client; ++i) {
        client = im_socket_connect_timeout("127.0.0.1", 18130, 100);
        if (!client) im_platform_sleep_ms(10);
    }
    if (!client) { headless_shutdown(); return 3; }
    im_socket_set_nonblocking(client, 1);
    const char *event = "{\"key\":\"left\",\"down\":1}\n";
    if (im_socket_send(client, event, strlen(event)) <= 0) { im_socket_close(client); headless_shutdown(); return 4; }
    int kind = 0;
    for (int i = 0; i < 50 && !kind; ++i) { kind = headless_poll_input(input, sizeof input); if (!kind) im_platform_sleep_ms(10); }
    if (kind != 1 || strstr(input, "left") == NULL) { im_socket_close(client); headless_shutdown(); return 5; }
    if (!headless_send_frame("{\"frame\":1}")) { im_socket_close(client); headless_shutdown(); return 6; }
    char out[64] = {0};
    for (int i = 0; i < 50 && !out[0]; ++i) { int n = im_socket_recv(client, out, sizeof out - 1); if (n > 0) out[n] = 0; else im_platform_sleep_ms(10); }
    int ok = strstr(out, "frame") != NULL;
    im_socket_close(client); headless_shutdown();
    if (!ok) return 7;
    puts("headless probe: ok"); return 0;
}
#endif
