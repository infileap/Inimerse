#include "socket.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ImSocket {
#ifdef _WIN32
    uintptr_t fd;
#else
    int fd;
#endif
};

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
static int socket_valid(uintptr_t fd) { return fd != INVALID_SOCKET; }
int im_socket_init(void) { WSADATA data; return WSAStartup(MAKEWORD(2,2), &data) == 0 ? 0 : -1; }
void im_socket_shutdown(void) { WSACleanup(); }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
static int socket_valid(int fd) { return fd >= 0; }
int im_socket_init(void) { return 0; }
void im_socket_shutdown(void) {}
#endif

static ImSocket *wrap_socket(
#ifdef _WIN32
    uintptr_t fd
#else
    int fd
#endif
) {
    if (!socket_valid(fd)) return NULL;
    ImSocket *socket = (ImSocket *)calloc(1, sizeof(*socket));
    if (!socket) {
#ifdef _WIN32
        closesocket((SOCKET)fd);
#else
        close(fd);
#endif
        return NULL;
    }
    socket->fd = fd; return socket;
}

static int resolve_addr(const char *host, uint16_t port, struct sockaddr_storage *out, socklen_t *out_len, int passive) {
    struct addrinfo hints = {0}, *result = NULL;
    char service[16]; snprintf(service, sizeof service, "%u", (unsigned)port);
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = passive ? AI_PASSIVE : 0;
    if (getaddrinfo((host && *host) ? host : NULL, service, &hints, &result) != 0 || !result) return -1;
    if (result->ai_addrlen > sizeof(*out)) { freeaddrinfo(result); return -1; }
    memcpy(out, result->ai_addr, result->ai_addrlen); *out_len = (socklen_t)result->ai_addrlen;
    freeaddrinfo(result); return 0;
}

ImSocket *im_socket_listen(const char *host, uint16_t port, int backlog) {
    struct sockaddr_storage addr; socklen_t addr_len;
    if (resolve_addr(host, port, &addr, &addr_len, 1) != 0) return NULL;
#ifdef _WIN32
    SOCKET fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP); if (fd == INVALID_SOCKET) return NULL;
    BOOL yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
#else
    int fd = socket(addr.ss_family, SOCK_STREAM, 0); if (fd < 0) return NULL;
    int yes = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    if (bind(fd, (struct sockaddr *)&addr, addr_len) != 0 || listen(fd, backlog > 0 ? backlog : 16) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return NULL;
    }
    return wrap_socket(fd);
}

ImSocket *im_socket_connect(const char *host, uint16_t port) {
    struct sockaddr_storage addr; socklen_t addr_len;
    if (resolve_addr(host, port, &addr, &addr_len, 0) != 0) return NULL;
#ifdef _WIN32
    SOCKET fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP); if (fd == INVALID_SOCKET) return NULL;
#else
    int fd = socket(addr.ss_family, SOCK_STREAM, 0); if (fd < 0) return NULL;
#endif
    if (connect(fd, (struct sockaddr *)&addr, addr_len) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        return NULL;
    }
    return wrap_socket(fd);
}

ImSocket *im_socket_accept(ImSocket *listener) {
    if (!listener) return NULL;
#ifdef _WIN32
    SOCKET fd = accept((SOCKET)listener->fd, NULL, NULL);
#else
    int fd = accept(listener->fd, NULL, NULL);
#endif
    return wrap_socket(fd);
}
int im_socket_send(ImSocket *socket, const void *data, size_t length) {
    if (!socket || !data) return -1;
#ifdef _WIN32
    return send((SOCKET)socket->fd, (const char *)data, (int)length, 0);
#else
    return (int)send(socket->fd, data, length, 0);
#endif
}
int im_socket_recv(ImSocket *socket, void *buffer, size_t capacity) {
    if (!socket || !buffer || !capacity) return -1;
#ifdef _WIN32
    return recv((SOCKET)socket->fd, (char *)buffer, (int)capacity, 0);
#else
    return (int)recv(socket->fd, buffer, capacity, 0);
#endif
}
int im_socket_set_nonblocking(ImSocket *socket, int enabled) {
    if (!socket) return -1;
#ifdef _WIN32
    u_long mode = enabled ? 1 : 0; return ioctlsocket((SOCKET)socket->fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(socket->fd, F_GETFL, 0); if (flags < 0) return -1; return fcntl(socket->fd, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#endif
}
int im_socket_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
void im_socket_close(ImSocket *socket) {
    if (!socket) return;
#ifdef _WIN32
    closesocket((SOCKET)socket->fd);
#else
    close(socket->fd);
#endif
    free(socket);
}
