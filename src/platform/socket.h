#ifndef INIMERSE_PLATFORM_SOCKET_H
#define INIMERSE_PLATFORM_SOCKET_H

#include <stddef.h>
#include <stdint.h>

typedef struct ImSocket ImSocket;

int im_socket_init(void);
void im_socket_shutdown(void);
ImSocket *im_socket_listen(const char *host, uint16_t port, int backlog);
ImSocket *im_socket_connect(const char *host, uint16_t port);
/* Connect with a bounded timeout in milliseconds. timeout_ms <= 0 uses 2s. */
ImSocket *im_socket_connect_timeout(const char *host, uint16_t port, int timeout_ms);
ImSocket *im_socket_accept(ImSocket *listener);
int im_socket_send(ImSocket *socket, const void *data, size_t length);
int im_socket_recv(ImSocket *socket, void *buffer, size_t capacity);
/* Wait until the socket has bytes to read or the deadline passes: 1 readable,
   0 timed out, -1 error.  timeout_ms < 0 waits without a deadline.  A blocking
   recv() cannot express "the peer connected and then said nothing", which is a
   failure mode, not a slow path. */
int im_socket_wait_readable(ImSocket *socket, int timeout_ms);
/* Monotonic milliseconds.  Lives here so a caller of the socket PAL can build a
   deadline without also linking the platform layer. */
uint64_t im_socket_now_ms(void);
/* Peek one byte without consuming it: >0 data, 0 closed, -1 error/would-block. */
int im_socket_peek(ImSocket *socket);
int im_socket_set_nonblocking(ImSocket *socket, int enabled);
int im_socket_last_error(void);
int im_socket_would_block(void);
int im_socket_local_port(ImSocket *socket);
/* Convenience probes used by servers without exposing native descriptors. */
int im_socket_port_open(const char *host, uint16_t port, int timeout_ms);
int im_socket_port_available(const char *host, uint16_t port);
void im_socket_close(ImSocket *socket);

#endif
