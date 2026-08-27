#ifndef INIMERSE_PLATFORM_SOCKET_H
#define INIMERSE_PLATFORM_SOCKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct ImSocket ImSocket;

int im_socket_init(void);
void im_socket_shutdown(void);
ImSocket *im_socket_listen(const char *host, uint16_t port, int backlog);
ImSocket *im_socket_connect(const char *host, uint16_t port);
ImSocket *im_socket_accept(ImSocket *listener);
int im_socket_send(ImSocket *socket, const void *data, size_t length);
int im_socket_recv(ImSocket *socket, void *buffer, size_t capacity);
/* Peek one byte without consuming it: >0 data, 0 closed, -1 error/would-block. */
int im_socket_peek(ImSocket *socket);
int im_socket_set_nonblocking(ImSocket *socket, int enabled);
int im_socket_last_error(void);
int im_socket_would_block(void);
int im_socket_local_port(ImSocket *socket);
void im_socket_close(ImSocket *socket);

#endif
