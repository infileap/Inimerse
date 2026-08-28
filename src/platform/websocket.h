#ifndef INIMERSE_PLATFORM_WEBSOCKET_H
#define INIMERSE_PLATFORM_WEBSOCKET_H
#include "socket.h"
#include <stddef.h>
int im_ws_accept(ImSocket *socket, const char *request, size_t request_len);
int im_ws_read_text(ImSocket *socket, char *out, size_t capacity);
int im_ws_send_pong(ImSocket *socket, const void *data, size_t length);
int im_ws_send_text(ImSocket *socket, const char *data, size_t length);
#endif
